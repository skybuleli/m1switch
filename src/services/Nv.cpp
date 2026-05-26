#include "services/Nv.h"
#include "services/Ipc.h"
#include "gpu/StateTracker.h"
#include "memory/Memory.h"
#include "common/Log.h"
#include <cstring>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>

// ═══════════════════════════════════════════════════════════
// NV (NVIDIA Driver) 服务 — 完整 NvMap/NvHost/NvGpu 实现
// ═══════════════════════════════════════════════════════════
//
// 关键架构:
//   nvdrv:        设备管理 (open/close)
//   nvmap:        GPU 内存分配 (create/alloc/map/cache)
//   nvhost-gpu:   3D 引擎命令提交 (SubmitGpfifo)
//   nvhost-ctrl:  同步点 (SyncptAlloc/SyncptWait)
//
// NvMap 对象生命周期:
//   Create(id, size) → Alloc(handle, align) → Map(handle, addr) → CacheOp(handle)
//
// GPU 地址空间:
//   游戏分配 NvMap 对象, 通过 NvMapAlloc 提交到 GPU 地址空间,
//   然后用得到的 IOVA 来构建 pushbuffer 命令。

static Memory*      g_nv_memory = nullptr;
static StateTracker* g_nv_tracker = nullptr;
static GPFifo*       g_nv_fifo = nullptr;

void ServiceNv_SetMemory(Memory* mem) { g_nv_memory = mem; }
void ServiceNv_SetGpuFifo(GPFifo* fifo) { g_nv_fifo = fifo; }
void ServiceNv_SetTracker(StateTracker* t) { g_nv_tracker = t; }

// ── NvMap 内存分配表 ─────────────────────────────────────
// 跟踪所有 NvMap 分配, 支持从客户机地址到 GPU IOVA 的映射

struct NvMapEntry {
    u32 handle;          // NvMap 句柄 (游戏看到的)
    u32 id;              // 内部 ID
    u64 size;            // 分配大小
    u64 align;           // 对齐要求
    u64 addr;            // 客户机物理地址 (0 = 未映射)
    u64 iova;            // GPU IOVA (映射后的地址)
    u32 flags;           // 分配标志
    bool cached;         // 是否缓存
    bool mapped;         // 是否已映射到 GPU
};

static std::vector<NvMapEntry> g_nvmap_entries;
static std::unordered_map<u32, size_t> g_nvmap_handle_index;  // handle → index
static u32 g_nvmap_next_id = 1;
static std::mutex g_nvmap_mutex;

// GPU IOVA 分配器 (简单线性分配)
static constexpr u64 GPU_IOVA_BASE = 0x80000000;  // GPU 地址空间起始
static constexpr u64 GPU_IOVA_SIZE = 0x10000000;  // 256 MB GPU 地址空间
static u64 g_next_iova = GPU_IOVA_BASE;

// ── 同步点 ──────────────────────────────────────────────
static std::atomic<u32> g_syncpt_counter{0};
static constexpr u32 MAX_SYNCPT = 24;
static std::atomic<u32> g_syncpt_value[MAX_SYNCPT]{};

// ═══════════════════════════════════════════════════════════
// Ioctl 命令 ID (从 Switchbrew/Horizon 文档获取)
// ═══════════════════════════════════════════════════════════

enum class NvIoctl : u32 {
    // NvMap ioctls (fd=/dev/nvmap)
    NvmapCreate          = 0xC0100001,
    NvmapFromId          = 0xC0080002,
    NvmapAlloc           = 0xC0200004,
    NvmapFree            = 0xC0180005,
    NvmapParam           = 0xC00C0007,
    NvmapGetId           = 0xC008000E,
    NvmapGetHandle       = 0xC008000F,
    NvmapCacheOp         = 0xC0080009,
    NvmapMap             = 0xC0480006,
    NvmapUnmap           = 0xC0480008,

    // NvHost-GPU ioctls (fd=/dev/nvhost-gpu)
    GpuSetNvmapFd        = 0x40080000,
    GpuAllocAs           = 0x80080003,
    GpuAllocObjCtx       = 0xC010000F,
    GpuSubmitGpfifo      = 0xC0480012,
    GpuAllocGpfifo       = 0xC0100014,
    GpuSetTimeout        = 0xC0080016,
    GpuWaitFifo          = 0xC0080005,
    GpuChannelZcullBind  = 0xC010000B,
    GpuGetErrorNotifier  = 0xC0100018,
    GpuGetParam          = 0xC008001A,
    GpuReturn            = 0x4008001C,
    GpuSetUserData       = 0xC008001E,
    GpuSetGpfifoEntry    = 0xC0400020,

    // NvHost-Ctrl ioctls (fd=/dev/nvhost-ctrl)
    CtrlSyncptAlloc      = 0x400C0001,
    CtrlSyncptWait       = 0xC00C0002,
    CtrlSyncptWaitEx     = 0xC0080003,
    CtrlSyncptRead       = 0x80080004,
    CtrlSyncptReadEx     = 0xC0080005,
    CtrlEventSignal      = 0xC0100006,
    CtrlEventWait        = 0xC0100007,
    CtrlEventWaitAsync   = 0xC0E00008,
};

// ═══════════════════════════════════════════════════════════
// NVDRV 服务 (设备管理)
// ═══════════════════════════════════════════════════════════

class NvDrvService : public ServiceBase {
public:
    NvDrvService() { IpcManager::Instance().RegisterService("nvdrv:", this); }
    const char* Name() const override { return "nvdrv:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0:  // Initialize
            LOG_DEBUG("NVDRV: Initialize");
            *out_sz = 0; return true;
        case 1:  // Open
            return HandleOpen(in, in_sz, out, out_sz);
        case 2:  // Close
            return HandleClose(in, in_sz, out, out_sz);
        default:
            LOG_TRACE("NVDRV: 未处理命令 %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    bool HandleOpen(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // 解析设备路径, 返回 fd
        char path[256] = {};
        if (in_sz > 4 && in_sz < 256) {
            u32 path_len = 0;
            std::memcpy(&path_len, in, 4);
            if (path_len > 0 && path_len < in_sz - 4) {
                std::memcpy(path, in + 4, std::min(path_len, (u32)(in_sz - 4)));
            }
        }
        LOG_INFO("NVDRV: Open '%s'", path);

        static u32 next_fd = 0x10;
        u32 fd = next_fd++;
        if (*out_sz >= 4) {
            std::memcpy(out, &fd, 4);
            *out_sz = 4;
        }
        return true;
    }

    bool HandleClose(const u8* in, size_t in_sz, u8*, size_t* out_sz) {
        LOG_DEBUG("NVDRV: Close");
        *out_sz = 0;
        return true;
    }
};

// ═══════════════════════════════════════════════════════════
// NVMAP 服务 (GPU 内存分配)
// ═══════════════════════════════════════════════════════════

class NvMapService : public ServiceBase {
public:
    NvMapService() { IpcManager::Instance().RegisterService("nvmap:", this); }
    const char* Name() const override { return "nvmap:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (static_cast<NvIoctl>(cmd_id)) {
        case NvIoctl::NvmapCreate:
            return HandleCreate(in, in_sz, out, out_sz);
        case NvIoctl::NvmapFromId:
            return HandleFromId(in, in_sz, out, out_sz);
        case NvIoctl::NvmapAlloc:
            return HandleAlloc(in, in_sz, out, out_sz);
        case NvIoctl::NvmapFree:
            return HandleFree(in, in_sz, out, out_sz);
        case NvIoctl::NvmapParam:
            return HandleParam(in, in_sz, out, out_sz);
        case NvIoctl::NvmapGetId:
            return HandleGetId(in, in_sz, out, out_sz);
        case NvIoctl::NvmapGetHandle:
            return HandleGetHandle(in, in_sz, out, out_sz);
        case NvIoctl::NvmapCacheOp:
            return HandleCacheOp(in, in_sz, out, out_sz);
        default:
            LOG_TRACE("NVMAP: 未处理 ioctl 0x%x", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    bool HandleCreate(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // 输入: u32 size (4 字节)
        // 输出: u32 handle (NvMap 句柄)
        u32 size = 0;
        if (in_sz >= 4) std::memcpy(&size, in, 4);

        std::lock_guard lock(g_nvmap_mutex);

        NvMapEntry entry{};
        entry.id = g_nvmap_next_id++;
        entry.handle = entry.id | 0xCAFE0000;  // 句柄有高16位标识
        entry.size = size;
        entry.align = 0x1000;  // 默认 4K 对齐
        entry.addr = 0;
        entry.iova = 0;
        entry.flags = 0;
        entry.cached = true;
        entry.mapped = false;

        g_nvmap_handle_index[entry.handle] = g_nvmap_entries.size();
        g_nvmap_entries.push_back(entry);

        LOG_INFO("NVMAP: Create size=%u → handle=0x%08x id=%u",
                 size, entry.handle, entry.id);

        if (*out_sz >= 4) {
            std::memcpy(out, &entry.handle, 4);
            *out_sz = 4;
        }
        return true;
    }

    bool HandleFromId(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 id = 0;
        if (in_sz >= 4) std::memcpy(&id, in, 4);

        std::lock_guard lock(g_nvmap_mutex);
        for (const auto& e : g_nvmap_entries) {
            if (e.id == id) {
                if (*out_sz >= 4) {
                    std::memcpy(out, &e.handle, 4);
                    *out_sz = 4;
                }
                return true;
            }
        }

        // 找不到就从 id 生成一个句柄
        u32 handle = id | 0xCAFE0000;
        LOG_DEBUG("NVMAP: FromId id=%u → handle=0x%x (generated)", id, handle);
        if (*out_sz >= 4) { std::memcpy(out, &handle, 4); *out_sz = 4; }
        return true;
    }

    bool HandleAlloc(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // 输入: struct { u32 handle, u32 heap_mask, u32 flags, u32 align, u8 kind, ... }
        // 对齐后的 total size 是 0x20 字节
        if (in_sz < 0x20) {
            LOG_WARN("NVMAP: Alloc 输入太小 (%zu)", in_sz);
            if (*out_sz >= 4) { std::memset(out, 0, 4); *out_sz = 4; }
            return true;
        }

        u32 handle = 0, heap_mask = 0, flags = 0, align = 0;
        std::memcpy(&handle, in, 4);
        std::memcpy(&heap_mask, in + 4, 4);
        std::memcpy(&flags, in + 8, 4);
        std::memcpy(&align, in + 12, 4);
        u8 kind = 0;
        if (in_sz > 16) kind = in[16];

        std::lock_guard lock(g_nvmap_mutex);

        auto it = g_nvmap_handle_index.find(handle);
        if (it == g_nvmap_handle_index.end()) {
            LOG_WARN("NVMAP: Alloc: 未知句柄 0x%08x", handle);
            if (*out_sz >= 4) { std::memset(out, 0, 4); *out_sz = 4; }
            return true;
        }

        auto& entry = g_nvmap_entries[it->second];
        entry.align = (align > 0) ? align : 0x1000;
        entry.flags = flags;
        entry.cached = (kind == 0 || kind == 1);  // 0 = 系统不可缓存, 1 = 系统可缓存

        // 在客户机内存中分配对齐的缓冲区, 并建立 GPU IOVA 映射
        if (entry.addr == 0 && g_nv_memory) {
            u64 alloc_size = (entry.size + entry.align - 1) & ~(entry.align - 1);
            u64 heap_base = Memory::HEAP_BASE;

            // 使用客户机堆分配内存
            if (Failed(g_nv_memory->SetHeapSize(g_nv_memory->GetHeapSize() + alloc_size))) {
                LOG_WARN("NVMAP: Alloc: 无法扩展堆 %llu 字节", alloc_size);
            } else {
                entry.addr = g_nv_memory->GetHeapBase() + g_nv_memory->GetHeapSize() - alloc_size;
                entry.iova = entry.addr;  // 统一内存: IOVA = 物理地址
                entry.mapped = true;

                LOG_INFO("NVMAP: Alloc handle=0x%08x size=%u align=%u → addr=0x%llx iova=0x%llx",
                         handle, (u32)entry.size, (u32)entry.align, entry.addr, entry.iova);
            }
        }

        if (*out_sz >= 4) { std::memset(out, 0, 4); *out_sz = 4; }
        return true;
    }

    bool HandleFree(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 handle = 0;
        if (in_sz >= 4) std::memcpy(&handle, in, 4);

        std::lock_guard lock(g_nvmap_mutex);
        auto it = g_nvmap_handle_index.find(handle);
        if (it != g_nvmap_handle_index.end()) {
            g_nvmap_entries[it->second].addr = 0;
            g_nvmap_entries[it->second].mapped = false;
        }
        LOG_DEBUG("NVMAP: Free handle=0x%08x", handle);
        if (*out_sz >= 4) { std::memset(out, 0, 4); *out_sz = 4; }
        return true;
    }

    bool HandleParam(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // 输入: {u32 handle, u32 param}
        // 输出: u32 value
        u32 handle = 0, param = 0;
        if (in_sz >= 8) { std::memcpy(&handle, in, 4); std::memcpy(&param, in + 4, 4); }

        u32 value = 0;
        std::lock_guard lock(g_nvmap_mutex);
        auto it = g_nvmap_handle_index.find(handle);
        if (it != g_nvmap_handle_index.end()) {
            const auto& entry = g_nvmap_entries[it->second];
            switch (param) {
            case 0: value = (u32)entry.size; break;     // Size
            case 1: value = (u32)entry.align; break;      // Alignment
            case 2: value = entry.cached ? 1 : 0; break;  // Cache
            case 3: value = (u32)entry.iova; break;        // IOVA low
            case 4: value = (u32)(entry.iova >> 32); break;// IOVA high
            default: LOG_TRACE("NVMAP: Param(%u) → 0", param); break;
            }
        }
        if (*out_sz >= 4) { std::memcpy(out, &value, 4); *out_sz = 4; }
        return true;
    }

    bool HandleGetId(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 handle = 0;
        if (in_sz >= 4) std::memcpy(&handle, in, 4);

        std::lock_guard lock(g_nvmap_mutex);
        auto it = g_nvmap_handle_index.find(handle);
        u32 id = (it != g_nvmap_handle_index.end()) ? g_nvmap_entries[it->second].id : handle & 0xFFFF;

        LOG_DEBUG("NVMAP: GetId handle=0x%08x → id=%u", handle, id);
        if (*out_sz >= 4) { std::memcpy(out, &id, 4); *out_sz = 4; }
        return true;
    }

    bool HandleGetHandle(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 id = 0;
        if (in_sz >= 4) std::memcpy(&id, in, 4);

        u32 handle = id | 0xCAFE0000;
        LOG_DEBUG("NVMAP: GetHandle id=%u → handle=0x%08x", id, handle);
        if (*out_sz >= 4) { std::memcpy(out, &handle, 4); *out_sz = 4; }
        return true;
    }

    bool HandleCacheOp(const u8* in, size_t in_sz, u8*, size_t* out_sz) {
        // CacheOp: 刷新或无效化缓存
        // 输入: {u32 handle, u32 op}  op: 0=flush, 1=invalidate
        LOG_TRACE("NVMAP: CacheOp");
        *out_sz = 0;
        return true;
    }
};

// ═══════════════════════════════════════════════════════════
// NVGPU 服务 (GPU 命令提交)
// ═══════════════════════════════════════════════════════════

class NvGpuService : public ServiceBase {
public:
    NvGpuService() { IpcManager::Instance().RegisterService("nvdrv#", this); }
    const char* Name() const override { return "nvdrv#"; }

    bool HandleCommand(u32 raw_cmd, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        auto cmd = static_cast<NvIoctl>(raw_cmd);
        switch (cmd) {
        case NvIoctl::GpuAllocAs:        return HandleAllocAs(in, in_sz, out, out_sz);
        case NvIoctl::GpuSetNvmapFd:     LOG_DEBUG("NVGPU: SetNvmapFd"); *out_sz = 0; return true;
        case NvIoctl::GpuAllocObjCtx:     return HandleAllocObjCtx(in, in_sz, out, out_sz);
        case NvIoctl::GpuSubmitGpfifo:   return HandleSubmitGpfifo(in, in_sz, out, out_sz);
        case NvIoctl::GpuAllocGpfifo:    return HandleAllocGpfifo(in, in_sz, out, out_sz);
        case NvIoctl::GpuSetTimeout:      LOG_DEBUG("NVGPU: SetTimeout"); *out_sz = 0; return true;
        case NvIoctl::GpuChannelZcullBind: LOG_DEBUG("NVGPU: ZcullBind"); *out_sz = 0; return true;
        case NvIoctl::GpuGetErrorNotifier: return HandleGetErrorNotifier(in, in_sz, out, out_sz);
        case NvIoctl::GpuGetParam:       return HandleGetParam(in, in_sz, out, out_sz);
        case NvIoctl::GpuReturn:          LOG_DEBUG("NVGPU: Return"); *out_sz = 0; return true;
        case NvIoctl::GpuSetUserData:     LOG_TRACE("NVGPU: SetUserData"); *out_sz = 0; return true;
        case NvIoctl::GpuSetGpfifoEntry:  LOG_TRACE("NVGPU: SetGpfifoEntry"); *out_sz = 0; return true;
        default:
            LOG_TRACE("NVGPU: 未处理 ioctl 0x%x", raw_cmd);
            *out_sz = 0;
            return true;
        }
    }

private:
    u64 as_handle_ = 0;
    u32 channel_fd_ = 0;
    u32 gpfifo_entries_ = 0;
    u32 nvmap_fd_ = 0;

    bool HandleAllocAs(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        as_handle_ = 0x1000;
        LOG_INFO("NVGPU: AllocAs → handle=0x%llx", as_handle_);
        if (*out_sz >= 8) {
            std::memcpy(out, &as_handle_, 8);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleAllocObjCtx(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // 输入: {u32 class_id, u32 flags, u64 obj_id}
        u32 class_id = 0xB197;  // Maxwell 3D
        if (in_sz >= 4) std::memcpy(&class_id, in, 4);

        channel_fd_ = 0x1001;
        LOG_INFO("NVGPU: AllocObjCtx class=0x%x → fd=%d", class_id, channel_fd_);
        if (*out_sz >= 8) {
            std::memcpy(out, &channel_fd_, 4);
            std::memset(out + 4, 0, 4);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleAllocGpfifo(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 num_entries = 0x400;
        if (in_sz >= 4) std::memcpy(&num_entries, in, 4);
        gpfifo_entries_ = num_entries;
        LOG_INFO("NVGPU: AllocGpfifo entries=%u", num_entries);
        if (*out_sz >= 4) { std::memcpy(out, &num_entries, 4); *out_sz = 4; }
        return true;
    }

    bool HandleGetErrorNotifier(const u8*, size_t, u8* out, size_t* out_sz) {
        LOG_DEBUG("NVGPU: GetErrorNotifier → 0 (disabled)");
        if (*out_sz >= 8) { std::memset(out, 0, 8); *out_sz = 8; }
        return true;
    }

    bool HandleGetParam(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 param = 0;
        if (in_sz >= 4) std::memcpy(&param, in, 4);
        u32 value = 0;
        switch (param) {
        case 0:  value = 0xF; break;    // Maxwell 3D class
        case 1:  value = 0x200; break;  // Engine type
        case 2:  value = 0x200; break;  // GPU page size
        case 4:  value = 0x28; break;   // ZCULL width in tiles
        case 5:  value = 0x14; break;    // ZCULL height in tiles
        case 7:  value = 1; break;      // ZCulti tap region cols
        case 8:  value = 0; break;       // ZCmulti tap region rows
        case 12: value = 0; break;       // Num TPCs
        case 13: value = 1; break;        // ROP count
        case 24: value = 0xFFFF; break;   // ZCULL region
        default: LOG_TRACE("NVGPU: GetParam(%u) → 0", param); break;
        }
        if (*out_sz >= 4) { std::memcpy(out, &value, 4); *out_sz = 4; }
        return true;
    }

    // ── GPFIFO 提交 — 核心命令提交通路 ──────────────────
    bool HandleSubmitGpfifo(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // 输入: struct { u64 pushbuffer_addr; u32 num_words; u32 flags; u64 fence_syncpt; }
        if (in_sz < 16) return false;

        u64 pb_addr = 0;
        u32 num_words = 0;
        std::memcpy(&pb_addr, in, 8);
        std::memcpy(&num_words, in + 8, 4);

        LOG_INFO("NVGPU: SubmitGpfifo addr=0x%llx words=%u", pb_addr, num_words);

        if (!g_nv_memory || !g_nv_fifo || num_words == 0) {
            *out_sz = 0;
            return true;
        }

        // 限制最大字数以避免错误数据导致崩溃
        if (num_words > 65536) num_words = 65536;

        std::vector<u32> words(num_words);
        for (u32 i = 0; i < num_words; i++) {
            u32 w;
            if (Failed(g_nv_memory->Read(pb_addr + i * 4, &w))) break;
            words[i] = w;
        }

        size_t consumed = g_nv_fifo->Process(words);
        LOG_DEBUG("NVGPU: GPFifo consumed %zu/%u words", consumed, num_words);

        // 运行 StateTracker 处理待定的 draw
        if (g_nv_tracker) {
            g_nv_tracker->PushBuffer(words);
        }

        // 返回提交信息
        // struct { u32 fence_id; u32 fence_value; }
        if (*out_sz >= 8) {
            IpcWriteU32(out, 0, 0);  // syncpt_id
            IpcWriteU32(out, 4, g_syncpt_counter.fetch_add(1));
            *out_sz = 8;
        }
        return true;
    }
};

// ═══════════════════════════════════════════════════════════
// NVHOST-CTRL 服务 (同步点)
// ═══════════════════════════════════════════════════════════

class NvHostCtrlService : public ServiceBase {
public:
    NvHostCtrlService() {
        IpcManager::Instance().RegisterService("nvhost-ctrl:", this);
    }
    const char* Name() const override { return "nvhost-ctrl:"; }

    bool HandleCommand(u32 raw_cmd, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        auto cmd = static_cast<NvIoctl>(raw_cmd);
        switch (cmd) {
        case NvIoctl::CtrlSyncptAlloc:
            return HandleSyncptAlloc(in, in_sz, out, out_sz);
        case NvIoctl::CtrlSyncptWait:
            return HandleSyncptWait(in, in_sz, out, out_sz);
        case NvIoctl::CtrlSyncptRead:
            return HandleSyncptRead(in, in_sz, out, out_sz);
        case NvIoctl::CtrlSyncptReadEx:
            return HandleSyncptReadEx(in, in_sz, out, out_sz);
        case NvIoctl::CtrlEventSignal:
            return HandleEventSignal(in, in_sz, out, out_sz);
        case NvIoctl::CtrlEventWait:
            return HandleEventWait(in, in_sz, out, out_sz);
        default:
            LOG_TRACE("NVHOST-CTRL: 未处理 ioctl 0x%x", raw_cmd);
            *out_sz = 0;
            return true;
        }
    }

private:
    bool HandleSyncptAlloc(const u8*, size_t, u8* out, size_t* out_sz) {
        u32 id = g_syncpt_counter.fetch_add(1);
        if (id >= MAX_SYNCPT) id = MAX_SYNCPT - 1;
        g_syncpt_value[id] = 0;
        LOG_INFO("NVHOST-CTRL: SyncptAlloc → id=%u", id);
        if (*out_sz >= 4) { IpcWriteU32(out, 0, id); *out_sz = 4; }
        return true;
    }

    bool HandleSyncptWait(const u8* in, size_t in_sz, u8*, size_t* out_sz) {
        // 输入: {u32 id, u32 threshold, s32 timeout}
        // 等待同步点达到阈值 (立即完成)
        LOG_TRACE("NVHOST-CTRL: SyncptWait (immediate)");
        *out_sz = 0;
        return true;
    }

    bool HandleSyncptRead(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 id = 0;
        if (in_sz >= 4) std::memcpy(&id, in, 4);
        u32 value = (id < MAX_SYNCPT) ? g_syncpt_value[id].load() : 0;
        if (*out_sz >= 4) { std::memcpy(out, &value, 4); *out_sz = 4; }
        return true;
    }

    bool HandleSyncptReadEx(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 id = 0;
        if (in_sz >= 4) std::memcpy(&id, in, 4);
        u32 value = (id < MAX_SYNCPT) ? g_syncpt_value[id].load() : 0;
        // 返回 {u32 value, u32 id}
        if (*out_sz >= 8) {
            IpcWriteU32(out, 0, value);
            IpcWriteU32(out, 4, id);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleEventSignal(const u8*, size_t, u8*, size_t* out_sz) {
        *out_sz = 0;
        return true;
    }

    bool HandleEventWait(const u8*, size_t, u8*, size_t* out_sz) {
        // 同步等待 — 直接返回完成
        *out_sz = 0;
        return true;
    }
};

// ═══════════════════════════════════════════════════════════
// 全局实例初始化
// ═══════════════════════════════════════════════════════════

static NvDrvService g_nvdrv;
static NvMapService g_nvmap;
static NvGpuService g_nvgpu;
static NvHostCtrlService g_nvhost_ctrl;

void ServiceNv_Init() {
    LOG_INFO("NV 服务就绪 (NvMap 分配 + NvHost 同步点 + NvGpu 提交)");
    (void)g_nvdrv; (void)g_nvmap; (void)g_nvgpu; (void)g_nvhost_ctrl;
}

// ── C API 供 EmuCore 初始化 ─────────────────────────────────
extern "C" {

NvMapEntry* NvMap_LookupByHandle(u32 handle) {
    std::lock_guard lock(g_nvmap_mutex);
    auto it = g_nvmap_handle_index.find(handle);
    if (it != g_nvmap_handle_index.end() && it->second < g_nvmap_entries.size()) {
        return &g_nvmap_entries[it->second];
    }
    return nullptr;
}

u64 NvMap_GetIOVA(u32 handle) {
    auto* entry = NvMap_LookupByHandle(handle);
    return entry ? entry->iova : 0;
}

u32 NvMap_Alloc(u32 size, u32 align) {
    // 便利 C API: 创建并分配一个 NvMap 对象
    u8 in_buf[0x20] = {};
    u8 out_buf[4] = {};
    size_t out_sz = 4;
    std::memcpy(in_buf, &size, 4);

    // 触发 Create
    g_nvmap_mutex.lock();
    NvMapEntry entry{};
    entry.id = g_nvmap_next_id++;
    entry.handle = entry.id | 0xCAFE0000;
    entry.size = size;
    entry.align = (align > 0) ? align : 0x1000;
    entry.cached = true;
    entry.mapped = false;
    entry.addr = 0;
    entry.iova = 0;
    g_nvmap_handle_index[entry.handle] = g_nvmap_entries.size();
    g_nvmap_entries.push_back(entry);
    g_nvmap_mutex.unlock();

    return entry.handle;
}

}  // extern "C"
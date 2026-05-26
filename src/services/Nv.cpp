#include "services/Nv.h"
#include "services/Ipc.h"
#include "gpu/StateTracker.h"
#include "common/Log.h"
#include <cstring>
#include <vector>

// ── NV (NVIDIA Driver) Service ──────────────────────────────
// Provides GPU channel + pushbuffer submission.
// Phase P0: minimal stub that accepts pushbuffer and feeds GPFifo.

static Memory*      g_nv_memory = nullptr;
static StateTracker* g_nv_tracker = nullptr;
static GPFifo*       g_nv_fifo = nullptr;

void ServiceNv_SetMemory(Memory* mem) { g_nv_memory = mem; }
void ServiceNv_SetGpuFifo(GPFifo* fifo) { g_nv_fifo = fifo; }
void ServiceNv_SetTracker(StateTracker* t) { g_nv_tracker = t; }

// ── Ioctl command IDs ───────────────────────────────────────
enum class NvIoctl : u32 {
    NvmapCreate = 0xC0100001,
    NvmapFromId = 0xC0080002,
    NvmapAlloc  = 0xC0200004,
    NvmapFree   = 0xC0180005,

    GpuAllocAs       = 0x80080003,
    GpuWaitFifo      = 0xC0080005,
    GpuChannelZcullBind = 0xC010000B,
    GpuSetNvmapFd    = 0x40080000,
    GpuAllocObjCtx   = 0xC010000F,
    GpuSubmitGpfifo  = 0xC0480012,
    GpuAllocGpfifo   = 0xC0100014,
    GpuSetTimeout    = 0xC0080016,
    GpuGetErrorNotifier = 0xC0100018,
    GpuGetParam      = 0xC008001A,
    GpuReturn         = 0x4008001C,
};

// ── NVDRV service (main control) ────────────────────────────
class NvDrvService : public ServiceBase {
public:
    NvDrvService() { IpcManager::Instance().RegisterService("nvdrv:", this); }
    const char* Name() const override { return "nvdrv:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            *out_sz = 0; return true;
        case 1: // Open /dev/nvmap
            return HandleOpen(in, in_sz, out, out_sz);
        case 2: // Open /dev/nvhost-gpu
            return HandleOpen(in, in_sz, out, out_sz);
        default:
            LOG_WARN("NVDRV: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    bool HandleOpen(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // Open a device node, returns fd
        if (*out_sz >= 4) {
            static int next_fd = 42;
            int fd = next_fd++;
            std::memcpy(out, &fd, 4);
            *out_sz = 4;
        }
        return true;
    }
};

// ── NVMAP service (memory allocator) ────────────────────────
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
        default:
            LOG_WARN("NVMAP: unhandled ioctl 0x%x", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    struct NvMapEntry {
        u32 id;
        u64 addr;    // guest physical
        u64 size;
    };
    std::vector<NvMapEntry> maps_;
    u32 next_id_ = 1;

    bool HandleCreate(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 size = 0;
        if (in_sz >= 4) std::memcpy(&size, in, 4);
        u32 id = next_id_++;
        maps_.push_back({id, 0, size});
        LOG_DEBUG("NVMAP: Create size=%u → id=%u", size, id);
        if (*out_sz >= 4) { std::memcpy(out, &id, 4); *out_sz = 4; }
        return true;
    }

    bool HandleFromId(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 id = 0;
        if (in_sz >= 4) std::memcpy(&id, in, 4);
        u32 handle = id | 0xCAFE0000;
        LOG_DEBUG("NVMAP: FromId id=%u → handle=0x%x", id, handle);
        if (*out_sz >= 4) { std::memcpy(out, &handle, 4); *out_sz = 4; }
        return true;
    }

    bool HandleAlloc(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        LOG_DEBUG("NVMAP: Alloc");
        if (*out_sz >= 4) { std::memset(out, 0, 4); *out_sz = 4; }
        return true;
    }
};

// ── NVGPU service (GPU command submission) ──────────────────
class NvGpuService : public ServiceBase {
public:
    NvGpuService() { IpcManager::Instance().RegisterService("nvdrv#", this); }
    const char* Name() const override { return "nvdrv#"; }

    bool HandleCommand(u32 raw_cmd, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        auto cmd = static_cast<NvIoctl>(raw_cmd);
        switch (cmd) {
        case NvIoctl::GpuAllocAs:
            return HandleAllocAs(in, in_sz, out, out_sz);
        case NvIoctl::GpuSetNvmapFd:
            *out_sz = 0; return true;
        case NvIoctl::GpuAllocObjCtx:
            return HandleAllocObjCtx(in, in_sz, out, out_sz);
        case NvIoctl::GpuSubmitGpfifo:
            return HandleSubmitGpfifo(in, in_sz, out, out_sz);
        case NvIoctl::GpuChannelZcullBind:
            *out_sz = 0; return true;
        case NvIoctl::GpuAllocGpfifo:
            return HandleAllocGpfifo(in, in_sz, out, out_sz);
        case NvIoctl::GpuSetTimeout:
            LOG_DEBUG("NVGPU: SetTimeout"); *out_sz = 0; return true;
        case NvIoctl::GpuGetErrorNotifier:
            return HandleGetErrorNotifier(in, in_sz, out, out_sz);
        case NvIoctl::GpuGetParam:
            return HandleGetParam(in, in_sz, out, out_sz);
        default:
            LOG_WARN("NVGPU: unhandled ioctl 0x%x", raw_cmd);
            *out_sz = 0;
            return true;
        }
    }

private:
    int channel_fd_ = 0;
    u64 as_handle_ = 0;
    u32 gpfifo_entries_ = 0;
    bool is_channel = false;

    bool HandleAllocAs(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        as_handle_ = 0x1000;
        LOG_DEBUG("NVGPU: AllocAs → 0x%llx", as_handle_);
        if (*out_sz >= 4) { std::memcpy(out, &as_handle_, 4); *out_sz = 4; }
        return true;
    }

    bool HandleAllocObjCtx(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        is_channel = true;
        channel_fd_ = 0x1001;
        LOG_DEBUG("NVGPU: AllocObjCtx → fd=%d", channel_fd_);
        if (*out_sz >= 4) { std::memcpy(out, &channel_fd_, 4); *out_sz = 4; }
        return true;
    }

    // ── AllocGpfifo: allocate a GPFIFO for the channel ──
    bool HandleAllocGpfifo(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 num_entries = 0x400; // default 1024 entries
        if (in_sz >= 4) std::memcpy(&num_entries, in, 4);
        gpfifo_entries_ = num_entries;
        LOG_DEBUG("NVGPU: AllocGpfifo entries=%u", num_entries);
        if (*out_sz >= 4) { std::memcpy(out, &num_entries, 4); *out_sz = 4; }
        return true;
    }

    // ── GetErrorNotifier: return an error notifier handle ──
    bool HandleGetErrorNotifier(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        LOG_DEBUG("NVGPU: GetErrorNotifier → 0 (disabled)");
        if (*out_sz >= 8) {
            std::memset(out, 0, 8);
            *out_sz = 8;
        }
        return true;
    }

    // ── GetParam: return GPU parameters ──
    bool HandleGetParam(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 param = 0;
        if (in_sz >= 4) std::memcpy(&param, in, 4);
        u32 value = 0;
        switch (param) {
        case 0:  value = 0xF; break;   // Maxwell GPU class (NV_GPU_CLASS_ID)
        case 1:  value = 0x200; break; // engine type (3D graphics)
        default: LOG_TRACE("NVGPU: GetParam(%u) → 0", param); break;
        }
        if (*out_sz >= 4) { std::memcpy(out, &value, 4); *out_sz = 4; }
        return true;
    }

    // ── The big one: GPFIFO submission ───────────────────
    static bool HandleSubmitGpfifo(const u8* in, size_t in_sz,
                                    u8* out, size_t* out_sz) {
        // Input: struct { u64 pushbuffer_addr; u32 num_words; ... }
        if (in_sz < 16) return false;

        u64 pb_addr = 0;
        u32 num_words = 0;
        std::memcpy(&pb_addr, in, 8);
        std::memcpy(&num_words, in + 8, 4);

        LOG_INFO("NVGPU: SubmitGpfifo addr=0x%llx words=%u",
                 pb_addr, num_words);

        if (!g_nv_memory || !g_nv_fifo || num_words == 0) {
            *out_sz = 0;
            return true;
        }

        // Read pushbuffer from guest memory and process it
        std::vector<u32> words(num_words);
        for (u32 i = 0; i < num_words && i < 8192; i++) {
            u32 w;
            if (Failed(g_nv_memory->Read(pb_addr + i * 4, &w))) break;
            words[i] = w;
        }

        size_t consumed = g_nv_fifo->Process(words);
        LOG_INFO("NVGPU: GPFifo consumed %zu/%u words", consumed, num_words);

        *out_sz = 0;
        return true;
    }
};

// ── Global instances ────────────────────────────────────────
static NvDrvService g_nvdrv;
static NvMapService g_nvmap;
static NvGpuService g_nvgpu;

void ServiceNv_Init() {
    LOG_INFO("NV service ready");
    (void)g_nvdrv; (void)g_nvmap; (void)g_nvgpu;
}

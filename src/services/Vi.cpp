#include "services/Ipc.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include <cstring>
#include <mutex>
#include <atomic>

// ═══════════════════════════════════════════════════════════
// VI (Display) Service — 完整 BufferQueue 实现
// ═══════════════════════════════════════════════════════════
//
// Switch 显示架构:
//   应用 (生产者) → VI BufferQueue → 合成器 (消费者) → 屏幕
//
// 关键 IPC 接口:
//   vi:u  — 用户态显示管理 (OpenDisplay, CreateLayer, etc.)
//   vi:m  — 管理态 (同上 + 管理功能)
//
// BufferQueue 有两种模式:
//   1. 共享内存模式: 游戏直接写入 VI 分配的帧缓冲
//   2. NvMap 模式: 游戏 GPU 渲染到 NvMap 对象, 提交给队列

static Memory* g_vi_memory = nullptr;
void ServiceVi_SetMemory(Memory* mem) { g_vi_memory = mem; }

// ── 帧缓冲配置 ──────────────────────────────────────────
static constexpr u64 FB_ADDR      = 0xE0000000;   // 帧缓冲基址
static constexpr u64 FB_SIZE      = 0x800000;      // 8 MiB (双缓冲 1280x720 BGRA8)
static constexpr u64 FB_ADDR_DUAL = 0xE0800000;    // 第二缓冲区
static constexpr u32 FB_WIDTH    = 1280;
static constexpr u32 FB_HEIGHT   = 720;
static constexpr u32 FB_STRIDE  = FB_WIDTH * 4;    // BGRA8 = 4 字节/像素

// ── BufferQueue 状态 ─────────────────────────────────────
// 模拟 Android BufferQueue 的生产者/消费者模型
struct BufferQueue {
    static constexpr u32 NUM_SLOTS = 3;   // 三缓冲
    static constexpr u32 SLOT_FREE = 0;
    static constexpr u32 SLOT_DEQUEUED = 1;
    static constexpr u32 SLOT_QUEUED = 2;
    static constexpr u32 SLOT_ACQUIRED = 3;

    struct Slot {
        u64 gpu_address = 0;     // NvMap 或帧缓冲地址
        u32 state = SLOT_FREE;
        u32 acquire_fence = 0;
        u32 release_fence = 0;
        s32 crop[4] = {};        // {left, top, right, bottom}
        u32 transform = 0;       // 0 = identity, 1 = rotate 90, etc.
        f32 scaling_mode = 0.0f;
    };

    Slot slots[NUM_SLOTS];
    u32 next_dequeue_slot = 0;
    std::mutex mutex;
    std::atomic<bool> connected{false};
};

static BufferQueue g_buffer_queue;
static u32 g_display_id = 1;    // 显示句柄
static u64 g_layer_id = 0x0100000000000000ULL;  // 层句柄
static bool g_layer_created = false;

// ── BufferQueueParcelable ──────────────────────────────────
// 序列化 BufferQueue 状态，供 IPC 传递
static void WriteBufferQueueParcelable(u8* out, size_t& out_size) {
    // Android Parcel 格式:
    //   u32: interface name length
    //   char[]: interface name ("android.gui.BufferQueue"
    //   u32: is_consumer_connected
    //   u32: num_slots
    //   u32: num_active_slots
    //   per-slot: {u32 state, u64 address}
    //   ... (fences, etc.)
    size_t pos = 0;
    // Interface name
    const char* iface = "android.gui.BufferQueue";
    u32 iface_len = (u32)strlen(iface);
    std::memcpy(out + pos, &iface_len, 4); pos += 4;
    std::memcpy(out + pos, iface, iface_len); pos += iface_len;
    // Consumer connected
    u32 connected = g_buffer_queue.connected ? 1 : 0;
    std::memcpy(out + pos, &connected, 4); pos += 4;
    // Num slots
    u32 num_slots = BufferQueue::NUM_SLOTS;
    std::memcpy(out + pos, &num_slots, 4); pos += 4;
    // Buffer data per slot
    for (u32 i = 0; i < BufferQueue::NUM_SLOTS; i++) {
        std::memcpy(out + pos, &g_buffer_queue.slots[i].gpu_address, 8); pos += 8;
        std::memcpy(out + pos, &g_buffer_queue.slots[i].state, 4); pos += 4;
    }
    // Width, height, format, transform
    IpcWriteU32(out, pos, FB_WIDTH);   pos += 4;
    IpcWriteU32(out, pos, FB_HEIGHT);  pos += 4;
    IpcWriteU32(out, pos, 4);          pos += 4;  // 像素格式 = RGBA8
    IpcWriteU32(out, pos, 0);          pos += 4;  // transform = identity

    out_size = pos;
}

// ── VI 服务实现 ───────────────────────────────────────────

class ViService : public ServiceBase {
public:
    ViService() : ServiceBase() {
        IpcManager::Instance().RegisterService("vi:", this);
        IpcManager::Instance().RegisterService("vi:m", this);
        IpcManager::Instance().RegisterService("vi:u", this);

        // 分配帧缓冲到客户机内存
        if (g_vi_memory) {
            g_vi_memory->MapPhysical(FB_ADDR, FB_SIZE, Memory::Permission::RW);
            g_vi_memory->MapPhysical(FB_ADDR_DUAL, FB_SIZE, Memory::Permission::RW);

            // 初始化帧缓冲为黑色
            auto* ptr0 = g_vi_memory->Pointer(FB_ADDR);
            auto* ptr1 = g_vi_memory->Pointer(FB_ADDR_DUAL);
            if (ptr0) std::memset(ptr0, 0, FB_SIZE);
            if (ptr1) std::memset(ptr1, 0, FB_SIZE);

            // 写入标记像素，使 EmuScreenView 检测到帧缓冲
            u32 marker = 0xFF000000;
            if (ptr0) std::memcpy(ptr0, &marker, 4);

            LOG_INFO("VI: 双缓冲帧缓冲 @ 0x%llx / 0x%llx (2x %zu KB)",
                     FB_ADDR, FB_ADDR_DUAL, (size_t)(FB_SIZE / 1024));

            // 初始化 BufferQueue 槽位
            for (u32 i = 0; i < BufferQueue::NUM_SLOTS; i++) {
                g_buffer_queue.slots[i].gpu_address = FB_ADDR + i * (FB_SIZE / 2);
                g_buffer_queue.slots[i].state = BufferQueue::SLOT_FREE;
            }
        }
    }

    const char* Name() const override { return "vi:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                        u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0:   return HandleInit(in, in_sz, out, out_sz);
        case 1:   return HandleOpenDisplay(in, in_sz, out, out_sz);
        case 2:   return HandleCloseDisplay(in, in_sz, out, out_sz);
        case 3:   return HandleGetDisplayResolution(in, in_sz, out, out_sz);
        case 10:  return HandleCreateLayer(in, in_sz, out, out_sz);
        case 11:  return HandleDestroyLayer(in, in_sz, out, out_sz);
        case 20:  return HandleOpenLayer(in, in_sz, out, out_sz);
        case 30:  return HandleGetDisplayVsyncEvent(in, in_sz, out, out_sz);
        case 40:  return HandleSetLayerZ(in, in_sz, out, out_sz);
        case 50:  return HandleGetDisplayMode(in, in_sz, out, out_sz);
        case 100: return HandleDequeueBuffer(in, in_sz, out, out_sz);
        case 101: return HandleQueueBuffer(in, in_sz, out, out_sz);
        case 102: return HandleCancelBuffer(in, in_sz, out, out_sz);
        case 110: return HandleAcquireBuffer(in, in_sz, out, out_sz);
        case 111: return HandleReleaseBuffer(in, in_sz, out, out_sz);
        case 120: return HandleConnect(in, in_sz, out, out_sz);
        case 121: return HandleDisconnect(in, in_sz, out, out_sz);
        case 130: return HandleSetPreallocatedBufferCount(in, in_sz, out, out_sz);
        default:
            LOG_WARN("VI: 未处理命令 %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    bool HandleInit(const u8*, size_t, u8* out, size_t* out_sz) {
        LOG_DEBUG("VI: Initialize");
        *out_sz = 0;
        return true;
    }

    bool HandleOpenDisplay(const u8*, size_t, u8* out, size_t* out_sz) {
        LOG_DEBUG("VI: OpenDisplay → id=%u", g_display_id);
        if (*out_sz >= 8) {
            std::memset(out, 0, 8);
            IpcWriteU32(out, 0, g_display_id);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleCloseDisplay(const u8*, size_t, u8*, size_t* out_sz) {
        LOG_DEBUG("VI: CloseDisplay");
        *out_sz = 0;
        return true;
    }

    bool HandleGetDisplayResolution(const u8*, size_t, u8* out, size_t* out_sz) {
        LOG_DEBUG("VI: GetDisplayResolution %ux%u", FB_WIDTH, FB_HEIGHT);
        if (*out_sz >= 8) {
            IpcWriteU32(out, 0, FB_WIDTH);
            IpcWriteU32(out, 4, FB_HEIGHT);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleCreateLayer(const u8*, size_t, u8* out, size_t* out_sz) {
        LOG_INFO("VI: CreateLayer → layer=0x%llx", g_layer_id);
        g_layer_created = true;
        if (*out_sz >= 8) {
            std::memcpy(out, &g_layer_id, 8);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleDestroyLayer(const u8*, size_t, u8*, size_t* out_sz) {
        LOG_DEBUG("VI: DestroyLayer");
        g_layer_created = false;
        *out_sz = 0;
        return true;
    }

    bool HandleOpenLayer(const u8*, size_t, u8* out, size_t* out_sz) {
        LOG_INFO("VI: OpenLayer → layer=0x%llx", g_layer_id);
        if (*out_sz >= 8) {
            std::memcpy(out, &g_layer_id, 8);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleGetDisplayVsyncEvent(const u8*, size_t, u8* out, size_t* out_sz) {
        // 返回一个可等待的事件句柄（模拟 vsync）
        LOG_TRACE("VI: GetDisplayVsyncEvent");
        if (*out_sz >= 8) {
            std::memset(out, 0, 8);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleSetLayerZ(const u8*, size_t, u8*, size_t* out_sz) {
        LOG_TRACE("VI: SetLayerZ");
        *out_sz = 0;
        return true;
    }

    bool HandleGetDisplayMode(const u8*, size_t, u8* out, size_t* out_sz) {
        LOG_DEBUG("VI: GetDisplayMode");
        if (*out_sz >= 0x38) {
            std::memset(out, 0, 0x38);
            // u32 width, u32 height, f32 refresh_rate, u32 pixel_format
            IpcWriteU32(out, 0x00, FB_WIDTH);          // width
            IpcWriteU32(out, 0x04, FB_HEIGHT);          // height
            f32 refresh = 60.0f;
            std::memcpy(out + 0x08, &refresh, 4);      // refresh rate
            IpcWriteU32(out, 0x0C, 4);                  // pixel format (BGRA8)
            IpcWriteU32(out, 0x10, 0);                  // index
            *out_sz = 0x38;
        }
        return true;
    }

    // ── BufferQueue 操作 ──────────────────────────────
    // 生产者 (游戏) 调用 DequeueBuffer 获取空闲缓冲区,
    // 渲染后调用 QueueBuffer 提交.
    // 消费者 (合成器/VI) 调用 AcquireBuffer 取帧,
    // 显示后调用 ReleaseBuffer 归还.

    bool HandleDequeueBuffer(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        std::lock_guard lock(g_buffer_queue.mutex);

        // 查找空闲槽位
        s32 slot = -1;
        for (u32 i = 0; i < BufferQueue::NUM_SLOTS; i++) {
            u32 idx = (g_buffer_queue.next_dequeue_slot + i) % BufferQueue::NUM_SLOTS;
            if (g_buffer_queue.slots[idx].state == BufferQueue::SLOT_FREE ||
                g_buffer_queue.slots[idx].state == BufferQueue::SLOT_ACQUIRED) {
                slot = (s32)idx;
                break;
            }
        }

        if (slot < 0) {
            // 无空闲缓冲区, 使用轮转: 强制下一个槽位
            slot = (s32)(g_buffer_queue.next_dequeue_slot % BufferQueue::NUM_SLOTS);
            LOG_WARN("VI: DequeueBuffer 无空闲, 强制使用槽位 %d", slot);
        }

        g_buffer_queue.slots[slot].state = BufferQueue::SLOT_DEQUEUED;
        g_buffer_queue.next_dequeue_slot = slot + 1;

        // 解析输入 (width, height, format, usage)
        u32 width = FB_WIDTH, height = FB_HEIGHT;
        if (in_sz >= 8) {
            width = IpcReadU32(in, 0);
            height = IpcReadU32(in, 4);
        }
        (void)width; (void)height;

        LOG_DEBUG("VI: DequeueBuffer → slot=%d addr=0x%llx", slot,
                  g_buffer_queue.slots[slot].gpu_address);

        // 输出: slot_index, fence, buffer_address
        if (*out_sz >= 24) {
            IpcWriteU32(out, 0, (u32)slot);            // slot index
            // fence (2x u32 = 0)
            IpcWriteU32(out, 4, 0);                     // acquire fence
            IpcWriteU32(out, 8, 0);                     // release fence
            // 帧缓冲地址
            u64 addr = g_buffer_queue.slots[slot].gpu_address;
            std::memcpy(out + 12, &addr, 8);
            IpcWriteU32(out, 20, FB_STRIDE);           // stride
            *out_sz = 24;
        }
        return true;
    }

    bool HandleQueueBuffer(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        std::lock_guard lock(g_buffer_queue.mutex);

        // 输入: slot_index, crop[4], transform, scaling_mode
        s32 slot = 0;
        if (in_sz >= 4) slot = (s32)IpcReadU32(in, 0);

        if (slot >= 0 && slot < (s32)BufferQueue::NUM_SLOTS) {
            g_buffer_queue.slots[slot].state = BufferQueue::SLOT_QUEUED;

            // 解析 crop 和 transform
            if (in_sz >= 24) {
                for (int i = 0; i < 4; i++)
                    g_buffer_queue.slots[slot].crop[i] = (s32)IpcReadU32(in, 4 + i * 4);
            }
            if (in_sz >= 44) {
                g_buffer_queue.slots[slot].transform = IpcReadU32(in, 20);
            }

            LOG_DEBUG("VI: QueueBuffer slot=%d addr=0x%llx",
                     slot, g_buffer_queue.slots[slot].gpu_address);
        }

        // 返回 release fence
        if (*out_sz >= 4) {
            IpcWriteU32(out, 0, 0);  // fence = 0 (no sync needed)
            *out_sz = 4;
        }
        return true;
    }

    bool HandleCancelBuffer(const u8* in, size_t in_sz, u8*, size_t* out_sz) {
        std::lock_guard lock(g_buffer_queue.mutex);
        s32 slot = 0;
        if (in_sz >= 4) slot = (s32)IpcReadU32(in, 0);
        if (slot >= 0 && slot < (s32)BufferQueue::NUM_SLOTS) {
            g_buffer_queue.slots[slot].state = BufferQueue::SLOT_FREE;
        }
        *out_sz = 0;
        return true;
    }

    bool HandleAcquireBuffer(const u8*, size_t, u8* out, size_t* out_sz) {
        std::lock_guard lock(g_buffer_queue.mutex);

        // 查找已排队的缓冲区
        s32 slot = -1;
        for (u32 i = 0; i < BufferQueue::NUM_SLOTS; i++) {
            if (g_buffer_queue.slots[i].state == BufferQueue::SLOT_QUEUED) {
                slot = (s32)i;
                break;
            }
        }

        if (slot < 0) {
            // 无已排队缓冲区
            if (*out_sz >= 4) {
                IpcWriteU32(out, 0, 0xFFFFFFFF);  // 无效槽位
                *out_sz = 4;
            }
            return true;
        }

        g_buffer_queue.slots[slot].state = BufferQueue::SLOT_ACQUIRED;

        // 输出: slot, fence, crop, transform, scaling_mode
        if (*out_sz >= 28) {
            IpcWriteU32(out, 0, (u32)slot);
            IpcWriteU32(out, 4, 0);  // acquire fence
            u64 addr = g_buffer_queue.slots[slot].gpu_address;
            std::memcpy(out + 8, &addr, 8);
            IpcWriteU32(out, 16, FB_STRIDE);
            IpcWriteU32(out, 20, FB_WIDTH);
            IpcWriteU32(out, 24, FB_HEIGHT);
            *out_sz = 28;
        }

        LOG_DEBUG("VI: AcquireBuffer → slot=%d", slot);
        return true;
    }

    bool HandleReleaseBuffer(const u8* in, size_t in_sz, u8*, size_t* out_sz) {
        std::lock_guard lock(g_buffer_queue.mutex);
        s32 slot = 0;
        if (in_sz >= 4) slot = (s32)IpcReadU32(in, 0);
        if (slot >= 0 && slot < (s32)BufferQueue::NUM_SLOTS) {
            g_buffer_queue.slots[slot].state = BufferQueue::SLOT_FREE;
            LOG_DEBUG("VI: ReleaseBuffer slot=%d", slot);
        }
        *out_sz = 0;
        return true;
    }

    bool HandleConnect(const u8*, size_t, u8* out, size_t* out_sz) {
        std::lock_guard lock(g_buffer_queue.mutex);
        g_buffer_queue.connected.store(true);

        // 返回 BufferQueue Parcelable
        WriteBufferQueueParcelable(out, *out_sz);
        return true;
    }

    bool HandleDisconnect(const u8*, size_t, u8*, size_t* out_sz) {
        g_buffer_queue.connected.store(false);
        *out_sz = 0;
        return true;
    }

    bool HandleSetPreallocatedBufferCount(const u8* in, size_t in_sz, u8*, size_t* out_sz) {
        u32 count = 3;
        if (in_sz >= 4) count = IpcReadU32(in, 0);
        LOG_DEBUG("VI: SetPreallocatedBufferCount=%u", count);
        *out_sz = 0;
        return true;
    }
};

// ── 全局实例 ────────────────────────────────────────────────
static ViService g_vi_service;

void ServiceVi_Init() {
    LOG_INFO("VI 服务就绪 (BufferQueue 双缓冲)");
    (void)g_vi_service;
}

// ── C API 供 EmuScreenView 查询 ─────────────────────────────
extern "C" {

u64 Vi_GetCurrentFramebuffer() {
    // 返回最近排队的缓冲区地址，供渲染扫描使用
    std::lock_guard lock(g_buffer_queue.mutex);
    for (u32 i = 0; i < BufferQueue::NUM_SLOTS; i++) {
        if (g_buffer_queue.slots[i].state == BufferQueue::SLOT_QUEUED ||
            g_buffer_queue.slots[i].state == BufferQueue::SLOT_ACQUIRED) {
            return g_buffer_queue.slots[i].gpu_address;
        }
    }
    // 回退: 返回第一个帧缓冲
    return FB_ADDR;
}

u32 Vi_GetFramebufferWidth()   { return FB_WIDTH; }
u32 Vi_GetFramebufferHeight()  { return FB_HEIGHT; }
u32 Vi_GetFramebufferStride()  { return FB_STRIDE; }

}  // extern "C"
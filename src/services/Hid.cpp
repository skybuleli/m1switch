#include "services/Ipc.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include "kernel/Kernel.h"
#include <cstring>

// ── Input subsystem C API ──────────────────────────────────
extern "C" {
void Input_Poll();
void Input_WriteToHidSharedMemory(u8* mem, u64 size);
}

// ── Npad 共享内存布局 (与 Horizon OS 一致) ────────────
// 偏移量参考 Switchbrew 和 Ryujinx

// SharedMemory 段偏移量
static constexpr u64 HID_TOUCH_OFFSET   = 0x00;     // TouchScreen
static constexpr u64 HID_TOUCH_SIZE     = 0x3000;
static constexpr u64 HID_MOUSE_OFFSET   = 0x3000;    // Mouse
static constexpr u64 HID_MOUSE_SIZE     = 0x400;
static constexpr u64 HID_KEYBOARD_OFFSET = 0x3400;   // Keyboard
static constexpr u64 HID_KEYBOARD_SIZE  = 0x800;
static constexpr u64 HID_NPAD_OFFSET    = 0x3C00;    // Npad (controller)
static constexpr u64 HID_NPAD_SIZE      = 0x22000;    // 8 controllers × 0x4500
static constexpr u64 HID_GYRO_OFFSET    = 0x25C00;    // SixAxisSensor
static constexpr u64 HID_GYRO_SIZE      = 0x6000;
static constexpr u64 HID_SLEEP_OFFSET   = 0x2BC00;    // SleepButton
static constexpr u64 HID_SLEEP_SIZE     = 0x400;

// Npad 状态结构 (每个控制器 0x4500 字节)
// 这是简化版本，只包含最常用的字段
struct NpadState {
    u64 timestamp;              // 0x00: 系统时钟时间戳
    u64 entry_count;            // 0x08: 条目数量
    u64 last_entry_index;       // 0x10: 最新条目索引
    u64 max_entry_count;       // 0x18: 最大条目数 (17)

    // 条目 (0-16)
    struct Entry {
        u64 timestamp;          // 条目时间戳
        u32 buttons;            // 按键位掩码
        u8 left_stick_x;       // 左摇杆 X (0-255, 128 = 中)
        u8 left_stick_y;       // 左摇杆 Y (0-255, 128 = 中)
        u8 right_stick_x;      // 右摇杆 X
        u8 right_stick_y;      // 右摇杆 Y
        u8 padding[4];
    } entries[17];

    // 填充到标准大小
    u8 _padding[0x4500 - 0x24 - 17 * sizeof(Entry)];
};

// 按键位掩码 (与 Horizon Npad 按键对应)
enum NpadButton : u32 {
    A          = 1 << 0,
    B          = 1 << 1,
    X          = 1 << 2,
    Y          = 1 << 3,
    LStick     = 1 << 4,
    RStick     = 1 << 5,
    L          = 1 << 6,
    R          = 1 << 7,
    ZL         = 1 << 8,
    ZR         = 1 << 9,
    Plus       = 1 << 10,
    Minus      = 1 << 11,
    DLeft      = 1 << 12,
    DUp        = 1 << 13,
    DRight     = 1 << 14,
    DDown      = 1 << 15,
    LStickLeft = 1 << 16,
    LStickUp   = 1 << 17,
    LStickRight= 1 << 18,
    LStickDown = 1 << 19,
    RStickLeft = 1 << 20,
    RStickUp   = 1 << 21,
    RStickRight= 1 << 22,
    RStickDown = 1 << 23,
    SL         = 1 << 24,
    SR         = 1 << 25,
};

static Memory* g_hid_memory = nullptr;
void ServiceHid_SetMemory(Memory* mem) { g_hid_memory = mem; }

// HID 共享内存 (固定地址)
static constexpr u64 HID_SHARED_MEM = 0xE1000000;
static constexpr u64 HID_SHARED_SIZE = 0x40000;

// 当前控制器状态 (由 Input_Poll 更新)
static u32 g_buttons = 0;
static u8 g_lx = 128, g_ly = 128, g_rx = 128, g_ry = 128;

class HidService : public ServiceBase {
public:
    HidService() {
        IpcManager::Instance().RegisterService("hid:", this);
        IpcManager::Instance().RegisterService("hidbus:", this);

        // 分配 HID 共享内存
        if (g_hid_memory) {
            g_hid_memory->MapPhysical(HID_SHARED_MEM, HID_SHARED_SIZE,
                                       Memory::Permission::RW);
            LOG_INFO("HID: 共享内存 @ 0x%llx (%u KB)", HID_SHARED_MEM, HID_SHARED_SIZE / 1024);

            auto* ptr = g_hid_memory->Pointer(HID_SHARED_MEM);
            if (ptr) std::memset(ptr, 0, HID_SHARED_SIZE);
        }

        // 创建 KSharedMemory 内核对象，供 GetSharedMemory 返回给 libnx
        hid_shmem_ = new KSharedMemory();
        hid_shmem_->address = 0;  // 由 libnx 通过 svcMapSharedMemory 指定
        hid_shmem_->phys_addr = HID_SHARED_MEM;
        hid_shmem_->size = HID_SHARED_SIZE;
        hid_shmem_->perm = Memory::Permission::RW;
        hid_shmem_handle_ = KernelHandleTable().Create(hid_shmem_);
        LOG_INFO("HID: shared memory handle=0x%x (phys=0x%llx size=0x%llx)",
                  hid_shmem_handle_, HID_SHARED_MEM, HID_SHARED_SIZE);
    }

    const char* Name() const override { return "hid:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                        u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0:   return HandleInitialize(in, in_sz, out, out_sz);
        case 1:   return HandleGetSharedMemory(in, in_sz, out, out_sz);
        case 2:   return HandleActivateNpad(in, in_sz, out, out_sz);
        case 3:   return HandleDeactivateNpad(in, in_sz, out, out_sz);
        case 4:   return HandleActivateTouchScreen(in, in_sz, out, out_sz);
        case 5:   return HandleSetNpadMode(in, in_sz, out, out_sz);
        case 10:  return HandleGetNpadState(in, in_sz, out, out_sz);
        case 11:  return HandleGetVibrationDeviceInfo(in, in_sz, out, out_sz);
        case 12:  return HandleVibrate(in, in_sz, out, out_sz);
        case 20:  return HandleSetNpadJoyAssignmentMode(in, in_sz, out, out_sz);
        case 21:  return HandleSetNpadJoyHoldType(in, in_sz, out, out_sz);
        case 30:  return HandleSetNpadHandheldActivationMode(in, in_sz, out, out_sz);
        case 100: return HandleCreateAppletResource(in, in_sz, out, out_sz);
        default:
            LOG_TRACE("HID: 未处理命令 %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    KSharedMemory* hid_shmem_ = nullptr;
    u32 hid_shmem_handle_ = 0;

    // HID 子会话服务: 接收 _hidGetSharedMemoryHandle (cmd 0)
    class HidAppletResourceService : public ServiceBase {
    public:
        HidAppletResourceService(KSharedMemory* shm) : shm_(shm) {}
        const char* Name() const override { return "HidAppletResource"; }
        bool HandleCommand(u32 cmd_id, const u8*, size_t, u8* out, size_t* out_sz) override {
            if (cmd_id == 0) {
                // _hidGetSharedMemoryHandle: 返回 KSharedMemory 的 copy handle
                u32 handle = KernelHandleTable().Create(shm_);
                LOG_DEBUG("HidAppletResource: cmd 0 → shmem handle=0x%x", handle);
                if (*out_sz >= 4) {
                    std::memcpy(out, &handle, sizeof(handle));
                    *out_sz = 4;
                }
                return true;
            }
            LOG_TRACE("HidAppletResource: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    private:
        KSharedMemory* shm_;
    };

    bool HandleInitialize(const u8*, size_t, u8* out, size_t* out_sz) {
        LOG_DEBUG("HID: HandleInitialize → create applet resource sub-session");
        // libnx v3.0.0 的 _hidCreateAppletResource (cmd 0) 期望返回子会话
        // 创建子会话服务并返回其句柄
        auto* sub = new HidAppletResourceService(hid_shmem_);
        u32 sub_session = IpcManager::Instance().CreateSession(sub);
        LOG_DEBUG("HID: applet resource sub-session=0x%x", sub_session);
        if (*out_sz >= 4) {
            std::memcpy(out, &sub_session, sizeof(sub_session));
            *out_sz = 4;
        }
        // 因为这是子会话创建，IPC handler 需要把这个作为 move handle 返回
        return true;
    }

    bool HandleGetSharedMemory(const u8*, size_t, u8* out, size_t* out_sz) {
        // 更新共享内存
        UpdateSharedMemory();

        LOG_DEBUG("HID: GetSharedMemory phys=0x%llx", HID_SHARED_MEM);

        // 返回 HID 共享内存物理地址 (handle 已在 Initialize 中返回)
        if (*out_sz >= 16) {
            std::memset(out, 0, 16);
            std::memcpy(out, &HID_SHARED_MEM, 8);
            *out_sz = 16;
        }
        return true;
    }

    bool HandleActivateNpad(const u8* in, size_t in_sz, u8*, size_t* out_sz) {
        // 输入: u64 npad_id_mask (哪些控制器被激活)
        u64 id_mask = 0x1F;  // 默认: Player 1-4 + Handheld
        if (in_sz >= 8) std::memcpy(&id_mask, in, 8);

        LOG_INFO("HID: ActivateNpad mask=0x%llx", id_mask);

        // 为每个激活的控制器初始化共享内存
        if (g_hid_memory) {
            auto* base = g_hid_memory->Pointer(HID_SHARED_MEM);
            if (base) {
                for (int i = 0; i < 8; i++) {
                    if (id_mask & (1ULL << i)) {
                        InitNpadState(base, i);
                    }
                }
            }
        }
        *out_sz = 0;
        return true;
    }

    bool HandleDeactivateNpad(const u8*, size_t, u8*, size_t* out_sz) {
        LOG_DEBUG("HID: DeactivateNpad");
        *out_sz = 0;
        return true;
    }

    bool HandleActivateTouchScreen(const u8*, size_t, u8*, size_t* out_sz) {
        LOG_DEBUG("HID: ActivateTouchScreen");
        *out_sz = 0;
        return true;
    }

    bool HandleSetNpadMode(const u8* in, size_t in_sz, u8*, size_t* out_sz) {
        u64 npad_id = 0x20;  // Handheld = 0x20
        u32 mode = 0;
        if (in_sz >= 8) std::memcpy(&npad_id, in, 8);
        if (in_sz >= 16) mode = IpcReadU32(in, 12);

        LOG_DEBUG("HID: SetNpadMode id=0x%llx mode=%u", npad_id, mode);
        *out_sz = 0;
        return true;
    }

    bool HandleGetNpadState(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // 更新共享内存后返回控制器状态
        UpdateSharedMemory();

        u64 npad_id = 0x20;
        if (in_sz >= 8) std::memcpy(&npad_id, in, 8);

        if (*out_sz >= 16) {
            IpcWriteU32(out, 0, g_buttons);
            out[4] = g_lx; out[5] = g_ly;
            out[6] = g_rx; out[7] = g_ry;
            *out_sz = 16;
        }
        return true;
    }

    bool HandleGetVibrationDeviceInfo(const u8*, size_t, u8* out, size_t* out_sz) {
        if (*out_sz >= 8) {
            std::memset(out, 0, 8);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleVibrate(const u8*, size_t, u8*, size_t* out_sz) {
        *out_sz = 0;
        return true;
    }

    bool HandleSetNpadJoyAssignmentMode(const u8*, size_t, u8*, size_t* out_sz) {
        *out_sz = 0;
        return true;
    }

    bool HandleSetNpadJoyHoldType(const u8*, size_t, u8*, size_t* out_sz) {
        *out_sz = 0;
        return true;
    }

    bool HandleSetNpadHandheldActivationMode(const u8*, size_t, u8*, size_t* out_sz) {
        *out_sz = 0;
        return true;
    }

    bool HandleCreateAppletResource(const u8*, size_t, u8* out, size_t* out_sz) {
        LOG_DEBUG("HID: CreateAppletResource");
        u32 handle = 0x02000000;
        if (*out_sz >= 4) {
            std::memcpy(out, &handle, 4);
            *out_sz = 4;
        }
        return true;
    }

    // ── 辅助方法 ─────────────────────────────────────────

    void InitNpadState(u8* base, u32 controller_idx) {
        if (!g_hid_memory) return;
        auto* npad_base = base + HID_NPAD_OFFSET + controller_idx * 0x4500;
        auto* npad = reinterpret_cast<NpadState*>(npad_base);

        npad->timestamp = 0;
        npad->max_entry_count = 17;
        npad->entry_count = 1;
        npad->last_entry_index = 0;

        for (int i = 0; i < 17; i++) {
            npad->entries[i].timestamp = 0;
            npad->entries[i].buttons = 0;
            npad->entries[i].left_stick_x = 128;
            npad->entries[i].left_stick_y = 128;
            npad->entries[i].right_stick_x = 128;
            npad->entries[i].right_stick_y = 128;
        }
    }

    void UpdateSharedMemory() {
        if (!g_hid_memory) return;
        auto* base = g_hid_memory->Pointer(HID_SHARED_MEM);
        if (!base) return;

        Input_Poll();
        Input_WriteToHidSharedMemory(base, HID_SHARED_SIZE);

        // 更新 Player 1 (控制器 0) 和 Handheld (控制器 8) 的 Npad 状态
        for (u32 idx : {0u, 8u}) {
            u64 offset = HID_NPAD_OFFSET + idx * 0x4500;
            if (offset + sizeof(NpadState) > HID_SHARED_SIZE) continue;

            auto* npad = reinterpret_cast<NpadState*>(base + offset);

            u64 now = 0; // mach_absolute_time 近似
            u64 entry_idx = npad->last_entry_index % 17;

            npad->timestamp = now;
            npad->last_entry_index = entry_idx;
            npad->entry_count = std::min(npad->entry_count + 1, (u64)17);

            auto& entry = npad->entries[entry_idx];
            entry.timestamp = now;
            entry.buttons = g_buttons;
            entry.left_stick_x = g_lx;
            entry.left_stick_y = g_ly;
            entry.right_stick_x = g_rx;
            entry.right_stick_y = g_ry;
        }
    }
};

// ── 全局实例 ────────────────────────────────────────────────
static HidService g_hid_service;

void ServiceHid_Init() {
    // 延迟映射 HID 共享内存 —— HidService 构造函数在静态初始化期间执行，
    // 此时 g_hid_memory 为 null，构造时的 MapPhysical 被跳过。
    if (g_hid_memory) {
        g_hid_memory->MapPhysical(HID_SHARED_MEM, HID_SHARED_SIZE,
                                   Memory::Permission::RW);
        auto* ptr = g_hid_memory->Pointer(HID_SHARED_MEM);
        if (ptr) std::memset(ptr, 0, HID_SHARED_SIZE);
        LOG_INFO("HID: 共享内存映射完成 @ 0x%llx (%u KB)",
                 HID_SHARED_MEM, HID_SHARED_SIZE / 1024);
    }
    LOG_INFO("HID 服务就绪 (Npad 共享内存布局)");
    (void)g_hid_service;
}

// ── C API 供 Input.mm 调用 ─────────────────────────────────
extern "C" {

void Hid_SetButtonState(u32 buttons) { g_buttons = buttons; }
void Hid_SetStickState(u8 lx, u8 ly, u8 rx, u8 ry) {
    g_lx = lx; g_ly = ly; g_rx = rx; g_ry = ry;
}

}  // extern "C"
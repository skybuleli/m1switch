#include "services/Ipc.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include <cstring>

// ── HID (Human Interface Device) Service ────────────────────
// Phase P0: minimal stub — returns empty input state.
// Homebrew initializes HID to get controller/touch input.

static Memory* g_hid_memory = nullptr;
void ServiceHid_SetMemory(Memory* mem) { g_hid_memory = mem; }

// HID SharedMemory layout (at a fixed address in guest space)
static constexpr u64 HID_SHARED_MEM = 0xE1000000;
static constexpr u64 HID_SHARED_SIZE = 0x40000;

class HidService : public ServiceBase {
public:
    HidService() {
        IpcManager::Instance().RegisterService("hid:", this);

        // Allocate HID shared memory
        if (g_hid_memory) {
            g_hid_memory->MapPhysical(HID_SHARED_MEM, HID_SHARED_SIZE,
                                       Memory::Permission::RW);
            LOG_INFO("HID: shared memory @ 0x%llx", HID_SHARED_MEM);

            // Initialize shared memory with zeroed state
            auto* ptr = g_hid_memory->Pointer(HID_SHARED_MEM);
            if (ptr) std::memset(ptr, 0, HID_SHARED_SIZE);
        }
    }

    const char* Name() const override { return "hid:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            LOG_DEBUG("HID: Initialize"); *out_sz = 0; return true;
        case 1: // GetSharedMemoryAddress
            return HandleGetSharedMem(in, in_sz, out, out_sz);
        case 2: // ActivateNpad
            *out_sz = 0; return true;
        case 3: // DeactivateNpad
            *out_sz = 0; return true;
        case 4: // ActivateTouchScreen
            *out_sz = 0; return true;
        case 5: // SetNpadMode
            *out_sz = 0; return true;
        case 10: // GetVibrationDeviceInfo
            *out_sz = 0; return true;
        case 11: // Vibrate
            *out_sz = 0; return true;
        case 30: // SetNpadHandheldActivationMode
            *out_sz = 0; return true;
        default:
            LOG_TRACE("HID: unhandled cmd %u", cmd_id);
            return false;
        }
    }

private:
    bool HandleGetSharedMem(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        LOG_DEBUG("HID: GetSharedMemoryAddress → 0x%llx", HID_SHARED_MEM);
        if (*out_sz >= 16) {
            std::memset(out, 0, 16);
            out[0] = (u8)(HID_SHARED_MEM >> 0);
            out[1] = (u8)(HID_SHARED_MEM >> 8);
            out[2] = (u8)(HID_SHARED_MEM >> 16);
            out[3] = (u8)(HID_SHARED_MEM >> 24);
            out[4] = (u8)(HID_SHARED_MEM >> 32);
            out[5] = (u8)(HID_SHARED_MEM >> 40);
            *out_sz = 16;
        }
        return true;
    }
};

static HidService g_hid_service;
void ServiceHid_Init() { LOG_INFO("HID service ready"); (void)g_hid_service; }

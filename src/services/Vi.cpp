#include "services/Ipc.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include <cstring>

// ── VI (Display) Service ────────────────────────────────────
// Manages display layers, framebuffer, BufferQueue.
// Phase P0: returns a framebuffer address in guest memory.
// The game writes pixels there; we blit them to screen.

static Memory* g_vi_memory = nullptr;
void ServiceVi_SetMemory(Memory* mem) { g_vi_memory = mem; }

// Framebuffer in guest memory (write-combined)
static constexpr u64 FB_ADDR = 0xE0000000;   // Top of guest 4GB
static constexpr u64 FB_SIZE = 0x100000;     // 1 MiB
static u32 fb_width   = 1280;
static u32 fb_height  = 720;

class ViService : public ServiceBase {
public:
    ViService() : ServiceBase() {
        IpcManager::Instance().RegisterService("vi:", this);
        IpcManager::Instance().RegisterService("vi:m", this);

        // Allocate framebuffer in guest memory
        if (g_vi_memory) {
            g_vi_memory->MapPhysical(FB_ADDR, FB_SIZE,
                                      Memory::Permission::RW);
            LOG_INFO("VI: framebuffer at 0x%llx (%u KB)", FB_ADDR, FB_SIZE/1024);
        }
    }

    const char* Name() const override { return "vi:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            return HandleInit(in, in_sz, out, out_sz);
        case 1: // OpenDisplay
            return HandleOpenDisplay(in, in_sz, out, out_sz);
        case 2: // CloseDisplay
            return true;
        case 10: // SetLayer
            return HandleSetLayer(in, in_sz, out, out_sz);
        default:
            LOG_TRACE("VI: unhandled cmd %u", cmd_id);
            return false;
        }
    }

private:
    bool HandleInit(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        LOG_DEBUG("VI: Initialize");
        *out_sz = 0;
        return true;
    }

    bool HandleOpenDisplay(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        LOG_DEBUG("VI: OpenDisplay");
        // Return display ID (0 = main)
        if (*out_sz >= 8) {
            std::memset(out, 0, 8);
            out[0] = 1;  // Display ID = 1 (main)
            *out_sz = 8;
        }
        return true;
    }

    bool HandleSetLayer(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // This is where the game gets the framebuffer address
        LOG_DEBUG("VI: SetLayer");

        // Return framebuffer address + size + stride
        if (*out_sz >= 32) {
            // 0x00: pixel format (u32) — 4 = RGBA8
            out[0] = 4; out[1] = 0; out[2] = 0; out[3] = 0;
            // 0x04: width
            IpcWriteU32(out, 0x04, fb_width);
            // 0x08: height
            IpcWriteU32(out, 0x08, fb_height);
            // 0x0C: stride (bytes per row)
            IpcWriteU32(out, 0x0C, fb_width * 4);
            // 0x10: address low
            IpcWriteU32(out, 0x10, (u32)(FB_ADDR & 0xFFFFFFFF));
            // 0x14: address high
            IpcWriteU32(out, 0x14, (u32)((FB_ADDR >> 32) & 0xFFFFFFFF));
            // 0x18-0x1C: zero
            *out_sz = 32;
        }
        return true;
    }
};

// ── Global instance ────────────────────────────────────────
static ViService g_vi_service;

void ServiceVi_Init() {
    LOG_INFO("VI service ready");
    (void)g_vi_service;
}

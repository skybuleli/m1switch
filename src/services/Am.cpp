#include "services/Ipc.h"
#include "common/Log.h"
#include <cstring>

// ── AM (Applet Manager) Service ─────────────────────────────
// Manages applet lifecycle, appearance, and system applets.
// Critical: most games call AM::Initialize, GetAppletResource,
// and various applet functions on startup.

class AmService : public ServiceBase {
public:
    AmService() {
        IpcManager::Instance().RegisterService("appletOE:", this);
        IpcManager::Instance().RegisterService("appletAE:", this);
        IpcManager::Instance().RegisterService("applet", this);
    }

    const char* Name() const override { return "appletOE:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            LOG_DEBUG("AM: Initialize");
            *out_sz = 0;
            return true;

        case 10: // OpenSystemApplet
            LOG_DEBUG("AM: OpenSystemApplet");
            *out_sz = 0;
            return true;

        case 40: // GetAppletResource
            LOG_DEBUG("AM: GetAppletResource");
            // Returns a handle — Phase 6: return dummy
            if (*out_sz >= 4) {
                std::memset(out, 0, 4);
                *out_sz = 4;
            }
            return true;

        case 100: // GetAppletType
            if (*out_sz >= 4) {
                out[0] = 2;  // Application applet type
                *out_sz = 4;
            }
            return true;

        case 150: // GetMainAppletIdentityInfo
            LOG_DEBUG("AM: GetMainAppletIdentityInfo");
            if (*out_sz >= 0x40) {
                std::memset(out, 0, 0x40);
                out[0] = 1;  // AppletID
                out[4] = 0;  // ApplicationKind
                *out_sz = 0x40;
            }
            return true;

        case 160: // SetScreenShotPermission
        case 170: // SetOperationModeChangedNotification
        case 180: // SetPerformanceModeChangedNotification
        case 200: // SetFocusHandlingMode
        case 210: // SetOutOfFocusSuspendingEnabled
        case 220: // SetAlbumImageOrientation
        case 240: // SetDesiredKeyboardLayout
        case 290: // SetVsyncInterruptionEvent
            *out_sz = 0;
            return true;

        default:
            LOG_TRACE("AM: unhandled cmd %u", cmd_id);
            return false;
        }
    }
};

// ── NS (Application) Service ────────────────────────────────
class NsService : public ServiceBase {
public:
    NsService() {
        IpcManager::Instance().RegisterService("ns:", this);
        IpcManager::Instance().RegisterService("ns:dev", this);
        IpcManager::Instance().RegisterService("ns:am2", this);
    }

    const char* Name() const override { return "ns:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            *out_sz = 0; return true;
        case 4: // GetApplicationControlData
            return HandleAppControlData(in, in_sz, out, out_sz);
        case 5: // GetApplicationDesiredLanguage
            if (*out_sz >= 8) {
                std::memset(out, 0, 8);
                out[0] = 1;  // Language code: 1 = English
                *out_sz = 8;
            }
            return true;
        case 6: // SetApplicationTerminateResult
            *out_sz = 0; return true;
        case 8: // GetNavigationEntityInfo
        case 9: // PushLaunchVersion
        case 10: // GetApplicationLaunchProperty
        case 11: // GetApplicationLaunchResult
            LOG_TRACE("NS: stub cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        default:
            LOG_TRACE("NS: unhandled cmd %u", cmd_id);
            return false;
        }
    }

private:
    bool HandleAppControlData(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        LOG_DEBUG("NS: GetApplicationControlData");
        // Return minimal control data (NACP)
        if (*out_sz >= 0x4000) {
            std::memset(out, 0, 0x4000);
            // Set application name (required for some games)
            const char* name = "M1Switch";
            std::memcpy(out + 0x208, name, std::min(strlen(name), (size_t)0x200));
            *out_sz = 0x4000;
        }
        return true;
    }
};

// ── LDR (Loader) Service ────────────────────────────────────
class LdrService : public ServiceBase {
public:
    LdrService() {
        IpcManager::Instance().RegisterService("ldr:", this);
        IpcManager::Instance().RegisterService("ldr:pm", this);
    }

    const char* Name() const override { return "ldr:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
        case 1: // LoadNro
        case 2: // LoadNso
        case 3: // UnloadNro
            *out_sz = 0; return true;
        case 7: // GetProcessInfo
            if (*out_sz >= 0x20) {
                std::memset(out, 0, 0x20);
                *out_sz = 0x20;
            }
            return true;
        default:
            LOG_TRACE("LDR: unhandled cmd %u", cmd_id);
            return false;
        }
    }
};

// ── Global instances ────────────────────────────────────────
static AmService g_am_service;
static NsService g_ns_service;
static LdrService g_ldr_service;

void ServiceAm_Init()  { LOG_INFO("AM service ready");  (void)g_am_service; }
void ServiceNs_Init()  { LOG_INFO("NS service ready");  (void)g_ns_service; }
void ServiceLdr_Init() { LOG_INFO("LDR service ready"); (void)g_ldr_service; }

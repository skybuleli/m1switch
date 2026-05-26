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
            // Return a valid applet resource handle (used by VI service)
            if (*out_sz >= 4) {
                u32 handle = 0x01000000; // IAppletResource handle
                std::memcpy(out, &handle, 4);
                *out_sz = 4;
            }
            return true;

        case 100: // GetAppletType
            if (*out_sz >= 4) {
                out[0] = 2;  // Application applet type
                *out_sz = 4;
            }
            return true;

        // ── IWindowController commands (separate interface, IDs start from 0) ──
        case 2:  // GetAppletResourceUserId  (IWindowController)
            if (*out_sz >= 8) {
                u64 uid = 1; // Default user ID
                std::memcpy(out, &uid, 8);
                *out_sz = 8;
            }
            return true;

        // ── IApplicationFunctions commands ──
        case 11:  // GetDisplayVersion  (IApplicationFunctions)
            if (*out_sz >= 16) {
                std::memset(out, 0, 16);
                out[0] = 1; out[1] = 0; out[2] = 0; out[3] = 0; // version 1.0.0
                *out_sz = 16;
            }
            return true;

        // ── ILibraryAppletSelfAccessor commands ──
        case 9:  // GetMainAppletIdentityInfo  (ILibraryAppletSelfAccessor)
            if (*out_sz >= 0x40) {
                std::memset(out, 0, 0x40);
                out[0] = 2;  // AppletType = Application
                out[4] = 0;  // ApplicationKind
                *out_sz = 0x40;
            }
            return true;

        // ── ILibraryAppletAccessor commands ──
        case 160: // GetIndirectLayerConsumerHandle  (ILibraryAppletAccessor)
            if (*out_sz >= 8) {
                u64 handle = 0x02000000;
                std::memcpy(out, &handle, 8);
                *out_sz = 8;
            }
            return true;

        // ── Common applet lifecycle commands ──
        case 26:  // RequestExitToSelf
        case 7:   // ExitProcessAndReturn
            LOG_DEBUG("AM: exit request");
            *out_sz = 0;
            return true;

        default:
            LOG_TRACE("AM: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
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
            *out_sz = 0;
            return true;
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
            *out_sz = 0;
            return true;
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

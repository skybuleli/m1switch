#include "services/Ipc.h"
#include "common/Log.h"
#include <cstring>

// ── AM (Applet Manager) Service ─────────────────────────────
// Manages applet lifecycle, appearance, and system applets.
// Critical: most games call AM::Initialize, GetAppletResource,
// and various applet functions on startup.
//
// Sub-object model:
//   appletOE:/appletAE:  → MainAppletService (Initialize, OpenSystemApplet, GetAppletResource, ...)
//     OpenSystemApplet   → IWindowController (separate anonymous session)
//     GetAppletResource  → ICommonStateGetter (separate anonymous session)
//   applet               → IApplicationFunctions
//
// On real HW, each sub-object lives on its own kernel object with
// independent cmd_id space. We model this via anonymous sessions.

// ── IWindowController ──────────────────────────────────────
// Created by OpenSystemApplet (cmd 10) on appletOE:
class WindowControllerService : public ServiceBase {
public:
    const char* Name() const override { return "IWindowController"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // GetAppletResourceUserId
            LOG_DEBUG("IWindowController: GetAppletResourceUserId");
            if (*out_sz >= 8) {
                u64 uid = 1; // Default user ID
                std::memcpy(out, &uid, 8);
                *out_sz = 8;
            }
            return true;

        case 1: // AcquireForegroundRights
            LOG_DEBUG("IWindowController: AcquireForegroundRights");
            *out_sz = 0;
            return true;

        case 2: // ReleaseForegroundRights
            LOG_DEBUG("IWindowController: ReleaseForegroundRights");
            *out_sz = 0;
            return true;

        case 10: // GetAppletResourceId (used by some games)
            if (*out_sz >= 4) {
                u32 id = 0; // Default resource ID
                std::memcpy(out, &id, 4);
                *out_sz = 4;
            }
            return true;

        case 20: // CreateManagedDisplayLayer
            // NOTE: On newer firmware this moved to vi:m.
            // But many games still call it via IWindowController.
            LOG_DEBUG("IWindowController: CreateManagedDisplayLayer");
            if (*out_sz >= 8) {
                u64 layer_id = 1; // Display layer ID
                std::memcpy(out, &layer_id, 8);
                *out_sz = 8;
            }
            return true;

        case 21: // CreateManagedDisplayLayer2 (some games)
            LOG_DEBUG("IWindowController: CreateManagedDisplayLayer2");
            if (*out_sz >= 8) {
                u64 layer_id = 2;
                std::memcpy(out, &layer_id, 8);
                *out_sz = 8;
            }
            return true;

        case 30: // GetIndirectLayerConsumerHandle
            if (*out_sz >= 8) {
                u64 handle = 0x02000000;
                std::memcpy(out, &handle, 8);
                *out_sz = 8;
            }
            return true;

        default:
            LOG_TRACE("IWindowController: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }
};

// ── ICommonStateGetter ─────────────────────────────────────
// Created by GetAppletResource (cmd 40) on appletOE:
class CommonStateGetterService : public ServiceBase {
public:
    const char* Name() const override { return "ICommonStateGetter"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // GetEventHandle
            LOG_DEBUG("ICommonStateGetter: GetEventHandle");
            // Return success with no handle (event not needed for MVP)
            *out_sz = 0;
            return true;

        case 1: // ReceiveMessage
            LOG_DEBUG("ICommonStateGetter: ReceiveMessage");
            // Return FocusStateChanged = 0xF
            if (*out_sz >= 4) {
                u32 msg = 0xF; // FocusStateChanged
                std::memcpy(out, &msg, 4);
                *out_sz = 4;
            }
            return true;

        case 5: // GetOperationMode
            if (*out_sz >= 4) {
                out[0] = 1; // 1 = Docked, 0 = Handheld
                *out_sz = 4;
            }
            return true;

        case 6: // GetPerformanceMode
            if (*out_sz >= 4) {
                out[0] = 1; // 1 = Boost (gives games full GPU), 0 = Normal
                *out_sz = 4;
            }
            return true;

        case 9: // SetFocusHandlingMode
            LOG_DEBUG("ICommonStateGetter: SetFocusHandlingMode");
            *out_sz = 0;
            return true;

        case 10: // SetOutOfFocusSuspendingEnabled
            LOG_DEBUG("ICommonStateGetter: SetOutOfFocusSuspendingEnabled");
            *out_sz = 0;
            return true;

        case 11: // GetDefaultDisplayResolution
            LOG_DEBUG("ICommonStateGetter: GetDefaultDisplayResolution");
            if (*out_sz >= 8) {
                u32 w = 1280;
                u32 h = 720;
                std::memcpy(out, &w, 4);
                std::memcpy(out + 4, &h, 4);
                *out_sz = 8;
            }
            return true;

        case 30: // GetOperationModeChangeEvent
            LOG_DEBUG("ICommonStateGetter: GetOperationModeChangeEvent");
            *out_sz = 0;
            return true;

        default:
            LOG_TRACE("ICommonStateGetter: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }
};

// ── Main Applet Service ────────────────────────────────────
class AmService : public ServiceBase {
public:
    AmService()
        : window_controller_(new WindowControllerService())
        , common_state_getter_(new CommonStateGetterService())
    {
        IpcManager::Instance().RegisterService("appletOE:", this);
        IpcManager::Instance().RegisterService("appletAE:", this);
    }

    ~AmService() {
        delete window_controller_;
        delete common_state_getter_;
    }

    const char* Name() const override { return "appletOE:"; }

    // Helper: write a u32 into the output buffer (little-endian)
    static void WriteU32(u8* out, u32 val, size_t* out_sz) {
        if (*out_sz >= 4) {
            out[0] = (val >> 0) & 0xFF;
            out[1] = (val >> 8) & 0xFF;
            out[2] = (val >> 16) & 0xFF;
            out[3] = (val >> 24) & 0xFF;
            *out_sz = 4;
        }
    }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        // ── Main applet interface ──
        case 0: // Initialize
            LOG_DEBUG("AM: Initialize");
            *out_sz = 0;
            return true;

        case 1: // GetStorageChannelEvent / GetAppletId
            LOG_DEBUG("AM: GetStorageChannelEvent");
            *out_sz = 0;
            return true;

        case 10: // OpenSystemApplet → returns IWindowController session
        {
            LOG_DEBUG("AM: OpenSystemApplet");
            u32 session = IpcManager::Instance().CreateSession(window_controller_);
            LOG_INFO("AM: OpenSystemApplet → IWindowController session=0x%x", session);
            WriteU32(out, session, out_sz);
            return true;
        }

        case 15: // PushInData / storage write (some games use this)
            LOG_DEBUG("AM: PushInData");
            *out_sz = 0;
            return true;

        case 40: // GetAppletResource → returns ICommonStateGetter session
        {
            LOG_DEBUG("AM: GetAppletResource");
            u32 session = IpcManager::Instance().CreateSession(common_state_getter_);
            LOG_INFO("AM: GetAppletResource → ICommonStateGetter session=0x%x", session);
            WriteU32(out, session, out_sz);
            return true;
        }

        case 100: // GetAppletType
            if (*out_sz >= 4) {
                out[0] = 2;  // Application applet type
                *out_sz = 4;
            }
            return true;

        case 200: // GetMainAppletIdentityInfo (some games call it on appletOE:)
            if (*out_sz >= 0x40) {
                std::memset(out, 0, 0x40);
                out[0] = 2;  // AppletType = Application
                out[4] = 0;  // ApplicationKind
                *out_sz = 0x40;
            }
            return true;

        // ── Exit / lifecycle ──
        case 7:  // ExitProcessAndReturn
        case 26: // RequestExitToSelf
            LOG_DEBUG("AM: exit request (cmd %u)", cmd_id);
            *out_sz = 0;
            return true;

        // ── IApplicationFunctions commands (also served on `applet` port) ──
        case 11: // GetDisplayVersion
            if (*out_sz >= 16) {
                std::memset(out, 0, 16);
                out[0] = 1; out[1] = 0; out[2] = 0; out[3] = 0; // version 1.0.0
                *out_sz = 16;
            }
            return true;

        // ── ILibraryAppletAccessor commands (also served on `applet` port) ──
        case 160: // GetIndirectLayerConsumerHandle
            if (*out_sz >= 8) {
                u64 handle = 0x02000000;
                std::memcpy(out, &handle, 8);
                *out_sz = 8;
            }
            return true;

        default:
            LOG_TRACE("AM: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    WindowControllerService* window_controller_;
    CommonStateGetterService* common_state_getter_;
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

// ── Applet Service (IApplicationFunctions) ─────────────────
// Served by the `applet` port. On real HW this is a different
// object with its own cmd_id space (cmd 0=Initialize,
// cmd 1=NotifyRunning, cmd 2=GetPseudoDeviceId, ...).
class AppletService : public ServiceBase {
public:
    AppletService() {
        IpcManager::Instance().RegisterService("applet", this);
    }

    const char* Name() const override { return "applet"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            LOG_DEBUG("Applet(IApplicationFunctions): Initialize");
            *out_sz = 0;
            return true;

        case 1: // NotifyRunning
            LOG_DEBUG("Applet(IApplicationFunctions): NotifyRunning");
            *out_sz = 0;
            return true;

        case 2: // GetPseudoDeviceId
            if (*out_sz >= 0x10) {
                std::memset(out, 0, 0x10);
                *out_sz = 0x10;
            }
            return true;

        case 10: // EnsureSaveData
            LOG_DEBUG("Applet(IApplicationFunctions): EnsureSaveData");
            if (*out_sz >= 8) {
                u64 save_data_size = 0x100000;
                std::memcpy(out, &save_data_size, 8);
                *out_sz = 8;
            }
            return true;

        case 11: // GetDisplayVersion
            if (*out_sz >= 16) {
                std::memset(out, 0, 16);
                out[0] = 1; out[1] = 0; out[2] = 0; out[3] = 0;
                *out_sz = 16;
            }
            return true;

        case 50: // EnsureSaveData2 / SaveDataRelated
            LOG_DEBUG("Applet(IApplicationFunctions): EnsureSaveData(2)");
            if (*out_sz >= 8) {
                u64 save_data_size = 0x100000;
                std::memcpy(out, &save_data_size, 8);
                *out_sz = 8;
            }
            return true;

        case 100: // SetTerminateResult
            LOG_DEBUG("Applet(IApplicationFunctions): SetTerminateResult");
            *out_sz = 0;
            return true;

        default:
            LOG_TRACE("Applet(IApplicationFunctions): unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }
};

// ── Global instances ────────────────────────────────────────
static AmService g_am_service;
static AppletService g_applet_service;
static NsService g_ns_service;
static LdrService g_ldr_service;

void ServiceAm_Init()  { LOG_INFO("AM service ready");  (void)g_am_service; }
void ServiceNs_Init()  { LOG_INFO("NS service ready");  (void)g_ns_service; }
void ServiceLdr_Init() { LOG_INFO("LDR service ready"); (void)g_ldr_service; }

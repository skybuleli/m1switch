#include "services/Ipc.h"
#include "common/Log.h"
#include <cstring>

// ── PCV (Power Control/Voltage) Service ─────────────────────
class PcvService : public ServiceBase {
public:
    PcvService() {
        IpcManager::Instance().RegisterService("pcv:", this);
    }
    const char* Name() const override { return "pcv:"; }
    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        LOG_TRACE("PCV: cmd %u", cmd_id);
        *out_sz = 0;
        return true;
    }
};

// ── PSC (Power State Control) Service ───────────────────────
class PscService : public ServiceBase {
public:
    PscService() {
        IpcManager::Instance().RegisterService("psc:", this);
    }
    const char* Name() const override { return "psc:"; }
    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        LOG_TRACE("PSC: cmd %u", cmd_id);
        *out_sz = 0;
        return true;
    }
};

// ── PM (Process Manager) Service ────────────────────────────
class PmService : public ServiceBase {
public:
    PmService() {
        IpcManager::Instance().RegisterService("pm:", this);
        IpcManager::Instance().RegisterService("pm:bm", this);
    }
    const char* Name() const override { return "pm:"; }
    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
        case 1: // GetCurrentAppletId
        case 2: // GetProcessId
        case 3: // GetApplicationPid
        case 4: // SetCurrentAppletId
        case 5: // SetApplicationPid
        case 6: // GetBootMode
            *out_sz = 0; return true;
        default:
            LOG_TRACE("PM: unhandled cmd %u", cmd_id);
            return false;
        }
    }
};

// ── NIFM (Network Interface) Service ────────────────────────
class NifmService : public ServiceBase {
public:
    NifmService() {
        IpcManager::Instance().RegisterService("nifm:", this);
        IpcManager::Instance().RegisterService("nifm:oa", this);
    }
    const char* Name() const override { return "nifm:"; }
    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
        case 3: // GetCurrentNetworkProfile
        case 4: // CreateRequest
        case 5: // SubmitRequest
        case 12: // SetNetworkProfile
            *out_sz = 0; return true;
        default:
            LOG_TRACE("NIFM: unhandled cmd %u", cmd_id);
            return false;
        }
    }
};

// ── RO (Read Only / Shared Library Loader) Service ─────────
class RoService : public ServiceBase {
public:
    RoService() {
        IpcManager::Instance().RegisterService("ro:", this);
    }
    const char* Name() const override { return "ro:"; }
    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
        case 2: // MapNro
        case 4: // MapNroByPid
        case 5: // UnmapNro
        case 6: // GetUnmapNroByPid
            *out_sz = 0; return true;
        default:
            LOG_TRACE("RO: unhandled cmd %u", cmd_id);
            return false;
        }
    }
};

// ── ERPT (Error Reporting) Service ──────────────────────────
class ErptService : public ServiceBase {
public:
    ErptService() {
        IpcManager::Instance().RegisterService("erpt:", this);
        IpcManager::Instance().RegisterService("erpt:s", this);
        IpcManager::Instance().RegisterService("erpt:c", this);
    }
    const char* Name() const override { return "erpt:"; }
    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        LOG_TRACE("ERPT: cmd %u", cmd_id);
        *out_sz = 0;
        return true;
    }
};

// ── Fatal Service ──────────────────────────────────────────
class FatalService : public ServiceBase {
public:
    FatalService() {
        IpcManager::Instance().RegisterService("fatal:", this);
        IpcManager::Instance().RegisterService("fatal:p", this);
    }
    const char* Name() const override { return "fatal:"; }
    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        LOG_TRACE("Fatal: cmd %u", cmd_id);
        *out_sz = 0;
        return true;
    }
};

// ── BTM (Bluetooth) Service ─────────────────────────────────
class BtmService : public ServiceBase {
public:
    BtmService() {
        IpcManager::Instance().RegisterService("btm:", this);
        IpcManager::Instance().RegisterService("btm:sys", this);
    }
    const char* Name() const override { return "btm:"; }
    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
        case 1: // GetDeviceInfo
        case 2: // GetChannelMap
        case 3: // EnableBluetooth
        case 4: // DisableBluetooth
            *out_sz = 0; return true;
        default:
            LOG_TRACE("BTM: unhandled cmd %u", cmd_id);
            return false;
        }
    }
};

// ── NFC (Near Field Communication) Service ──────────────────
class NfcService : public ServiceBase {
public:
    NfcService() {
        IpcManager::Instance().RegisterService("nfc:", this);
        IpcManager::Instance().RegisterService("nfc:mf", this);
        IpcManager::Instance().RegisterService("nfc:user", this);
    }
    const char* Name() const override { return "nfc:"; }
    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
        case 1: // StartDetection
        case 2: // StopDetection
        case 10: // GetTagInfo
        case 20: // GetNpadId
            *out_sz = 0; return true;
        default:
            LOG_TRACE("NFC: unhandled cmd %u", cmd_id);
            return false;
        }
    }
};

// ── Global instances ────────────────────────────────────────
static PcvService    g_pcv;
static PscService    g_psc;
static PmService     g_pm;
static NifmService   g_nifm;
static RoService     g_ro;
static ErptService   g_erpt;
static FatalService  g_fatal;
static BtmService    g_btm;
static NfcService    g_nfc;

void ServicePcv_Init()  { LOG_INFO("PCV ready");  (void)g_pcv; }
void ServicePsc_Init()  { LOG_INFO("PSC ready");  (void)g_psc; }
void ServicePm_Init()   { LOG_INFO("PM ready");   (void)g_pm; }
void ServiceNifm_Init() { LOG_INFO("NIFM ready"); (void)g_nifm; }
void ServiceRo_Init()   { LOG_INFO("RO ready");   (void)g_ro; }
void ServiceErpt_Init() { LOG_INFO("ERPT ready"); (void)g_erpt; }
void ServiceFatal_Init(){ LOG_INFO("Fatal ready");(void)g_fatal; }
void ServiceBtm_Init()  { LOG_INFO("BTM ready");  (void)g_btm; }
void ServiceNfc_Init()  { LOG_INFO("NFC ready");  (void)g_nfc; }

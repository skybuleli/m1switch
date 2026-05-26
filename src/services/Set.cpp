#include "services/Ipc.h"
#include "common/Log.h"
#include <cstring>
#include <ctime>

// ── SET (System Settings) Service ───────────────────────────
// Returns system language, timezone, etc.

static u64 GetLangCodeEnUs() { return 0x00000053552D6E65ULL; } // "en-US"

// set:sys GetFirmwareVersion 响应结构
struct NX_FirmwareVersion {
    u8 major, minor, micro, pad1;
    u8 rev_major, rev_minor, pad2, pad3;
    char platform[0x20];
    char version_hash[0x40];
    char display_version[0x18];
    char display_title[0x80];
};

class SetService : public ServiceBase {
    bool is_sys_;
public:
    SetService() : is_sys_(false) {
        IpcManager::Instance().RegisterService("set:", this);
    }
    explicit SetService(bool is_sys) : is_sys_(true) {
        IpcManager::Instance().RegisterService("set:sys", this);
    }
    const char* Name() const override { return is_sys_ ? "set:sys" : "set:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        if (is_sys_) return HandleSys(cmd_id, in, in_sz, out, out_sz);
        else         return HandleStd(cmd_id, in, in_sz, out, out_sz);
    }

    // "set:" — 标准设置服务
    bool HandleStd(u32 cmd_id, const u8* in, size_t in_sz,
                   u8* out, size_t* out_sz) {
        switch (cmd_id) {
        case 0: // GetLanguageCode → u64 BCP-47
            if (*out_sz >= 8) {
                u64 lang = GetLangCodeEnUs();
                std::memcpy(out, &lang, 8); *out_sz = 8;
            } return true;
        case 4: // GetRegionCode → u32
            if (*out_sz >= 4) {
                u32 region = 1; // USA
                std::memcpy(out, &region, 4); *out_sz = 4;
            } return true;
        default:
            LOG_TRACE("SET: unhandled cmd %u", cmd_id);
            *out_sz = 0; return true;
        }
    }

    // "set:sys" — 系统设置服务（命令 ID 完全不同！）
    bool HandleSys(u32 cmd_id, const u8* in, size_t in_sz,
                   u8* out, size_t* out_sz) {
        switch (cmd_id) {
        case 0: // SetLanguageCode (u64 input)
        case 1: // SetNetworkSettings
        case 2: // GetNetworkSettings
            *out_sz = 0; return true;
        case 3: // GetFirmwareVersion (old, <3.0.0)
        case 4: // GetFirmwareVersion (new, >=3.0.0)
            if (*out_sz >= sizeof(NX_FirmwareVersion)) {
                auto* fw = reinterpret_cast<NX_FirmwareVersion*>(out);
                memset(fw, 0, sizeof(*fw));
                fw->major = 15; fw->minor = 0; fw->micro = 0;
                strncpy(fw->platform, "NX", sizeof(fw->platform)-1);
                strncpy(fw->display_version, "15.0.0", sizeof(fw->display_version)-1);
                strncpy(fw->display_title, "NintendoSDK Firmware for NX 15.0.0",
                        sizeof(fw->display_title)-1);
                *out_sz = sizeof(*fw);
            } return true;
        default:
            LOG_TRACE("SETSYS: unhandled cmd %u", cmd_id);
            *out_sz = 0; return true;
        }
    }
};

// ── APM (Performance) Service ───────────────────────────────
class ApmService : public ServiceBase {
public:
    ApmService() {
        IpcManager::Instance().RegisterService("apm:", this);
        IpcManager::Instance().RegisterService("apm:sys", this);
    }
    const char* Name() const override { return "apm:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // OpenSession — 返回子会话句柄作为 move handle
            if (*out_sz >= 4) {
                u32 sub_session = IpcManager::Instance().OpenSessionFor("apm:");
                LOG_DEBUG("APM: OpenSession → sub_session 0x%x", sub_session);
                std::memcpy(out, &sub_session, sizeof(sub_session));
                *out_sz = 4;
            }
            return true;
        case 1: // GetPerformanceMode
            if (*out_sz >= 4) {
                u32 mode = 0; // 0 = Normal
                std::memcpy(out, &mode, sizeof(mode));
                *out_sz = 4;
            }
            return true;
        case 2: // SetCpuBoostMode
        case 3: // SetCpuBoostMode
            *out_sz = 0; return true;
        default:
            LOG_TRACE("APM: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }
};

// ── TIME (Clock) Service ────────────────────────────────────
class TimeService : public ServiceBase {
public:
    TimeService() {
        IpcManager::Instance().RegisterService("time:", this);
        IpcManager::Instance().RegisterService("time:a", this);
    }
    const char* Name() const override { return "time:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            *out_sz = 0; return true;
        case 1: // GetStandardTime
        case 2: // GetStandardLocalTime
            if (*out_sz >= 8) {
                // Return current time (epoch seconds * 1e9)
                u64 clock = (u64)time(nullptr) * 1000000000ULL;
                std::memcpy(out, &clock, 8);
                *out_sz = 8;
            }
            return true;
        default:
            LOG_TRACE("TIME: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }
};

// ── Global instances ────────────────────────────────────────
static SetService  g_set_service;       // registers "set:"
static SetService  g_setsys_service(true); // registers "set:sys"
static ApmService  g_apm_service;
static TimeService g_time_service;

void ServiceSet_Init()  { LOG_INFO("SET service ready");  (void)g_set_service; }
void ServiceApm_Init()  { LOG_INFO("APM service ready");  (void)g_apm_service; }
void ServiceTime_Init() { LOG_INFO("TIME service ready"); (void)g_time_service; }

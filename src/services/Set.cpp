#include "services/Ipc.h"
#include "common/Log.h"
#include <cstring>
#include <ctime>

// ── SET (System Settings) Service ───────────────────────────
// Returns system language, timezone, etc.

class SetService : public ServiceBase {
public:
    SetService() {
        IpcManager::Instance().RegisterService("set:", this);
        IpcManager::Instance().RegisterService("set:sys", this);
    }
    const char* Name() const override { return "set:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            *out_sz = 0; return true;
        case 1: // GetAvailableLanguageCodes
            if (*out_sz >= 16) {
                std::memset(out, 0, 16);
                out[0] = 1; out[4] = 1;  // 1 language: en-US
                out[8] = 0; out[9] = 0;  // en-US = 0
                out[10] = 0; out[11] = 0;
                *out_sz = 16;
            }
            return true;
        case 2: // MakeLanguageCode
            if (*out_sz >= 8) {
                std::memset(out, 0, 8);
                *out_sz = 8;
            }
            return true;
        case 3: // GetLanguageCode
            if (*out_sz >= 8) {
                u64 lang = 0; // en-US = 0
                std::memcpy(out, &lang, 8);
                *out_sz = 8;
            }
            return true;
        case 4: // GetRegionCode
            if (*out_sz >= 4) {
                u32 region = 1; // USA region
                std::memcpy(out, &region, 4);
                *out_sz = 4;
            }
            return true;
        default:
            LOG_TRACE("SET: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
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
static SetService  g_set_service;
static ApmService  g_apm_service;
static TimeService g_time_service;

void ServiceSet_Init()  { LOG_INFO("SET service ready");  (void)g_set_service; }
void ServiceApm_Init()  { LOG_INFO("APM service ready");  (void)g_apm_service; }
void ServiceTime_Init() { LOG_INFO("TIME service ready"); (void)g_time_service; }

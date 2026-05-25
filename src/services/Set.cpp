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
            if (*out_sz >= 8) {
                // Return 1 language (English = 1)
                out[0] = 1; out[1] = 0; out[2] = 0; out[3] = 0;
                out[4] = 1; out[5] = 0; out[6] = 0; out[7] = 0;
                *out_sz = 8;
            }
            return true;
        case 2: // MakeLanguageCode
            if (*out_sz >= 8) {
                out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 0;
                out[4] = 0; out[5] = 0; out[6] = 0; out[7] = 0;
                *out_sz = 8;
            }
            return true;
        default:
            LOG_TRACE("SET: unhandled cmd %u", cmd_id);
            return false;
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
        case 0: // Initialize
        case 1: // OpenSession
        case 2: // GetPerformanceMode
        case 3: // SetCpuBoostMode
            *out_sz = 0; return true;
        default:
            LOG_TRACE("APM: unhandled cmd %u", cmd_id);
            return false;
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
            return false;
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

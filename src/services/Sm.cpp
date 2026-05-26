#include "services/Ipc.h"
#include "common/Log.h"
#include <cstring>

// ── SM (Service Manager) ────────────────────────────────────
// Provides: sm:, sm:m:
//
// sm:   - RegisterService / UnregisterService / Wait / ...
// sm:m: - Manage service access control

class SmService : public ServiceBase {
public:
    SmService() {
        // Only register SM itself here — don't pre-register other services.
        // Each service registers itself via its static constructor (per .cpp file).
        // Pre-registration with nullptr can overwrite valid pointers depending
        // on static init order across translation units.
        IpcManager::Instance().RegisterService("sm:", this);
        IpcManager::Instance().RegisterService("sm:m:", this);
    }

    const char* Name() const override { return "sm:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            LOG_DEBUG("SM: Initialize");
            // Write result word (0 = success)
            if (*out_sz >= 4) {
                memset(out, 0, 4);
                *out_sz = 4;
            }
            return true;

        case 2: // GetService
            return HandleGetService(in, in_sz, out, out_sz);

        case 3: // RegisterService
            return HandleRegisterService(in, in_sz, out, out_sz);

        default:
            LOG_WARN("SM: unhandled cmd %u", cmd_id);
            return false;
        }
    }

private:
    bool HandleGetService(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // Input: service name string
        // Output: session handle
        if (in_sz < 1) return false;
        std::string name(reinterpret_cast<const char*>(in), in_sz);
        // Null-terminate at first null
        name = name.c_str();

        // Resolve service name to init function and call EnsureService
        if (name == "vi:" || name == "vi:m")            EnsureService(name.c_str(), ViInitialize);
        else if (name == "nvdrv:" || name == "nvdrv#")   EnsureService(name.c_str(), NvInitialize);
        else if (name == "fsp-srv:" || name == "fs:")      EnsureService(name.c_str(), FsInitialize);
        else if (name == "hid:")      EnsureService(name.c_str(), HidInitialize);
        else if (name == "set:" || name == "set:sys") EnsureService(name.c_str(), SetInitialize);
        else if (name == "apm:" || name == "apm:sys") EnsureService(name.c_str(), ApmInitialize);
        else if (name == "time:" || name == "time:a") EnsureService(name.c_str(), TimeInitialize);
        else if (name == "audout:" || name == "audren:") EnsureService(name.c_str(), AudioOutInitialize);
        else if (name == "appletOE:" || name == "appletAE:" || name == "applet") EnsureService(name.c_str(), AmInitialize);
        else if (name == "ns:" || name == "ns:dev" || name == "ns:am2") EnsureService(name.c_str(), NsInitialize);
        else if (name == "ldr:" || name == "ldr:pm") EnsureService(name.c_str(), LdrInitialize);
        else if (name == "spl:" || name == "spl:mig" || name == "spl:fs") EnsureService(name.c_str(), SplInitialize);
        else if (name == "acc:u0" || name == "acc:u1" || name == "acc:su") EnsureService(name.c_str(), AccountInitialize);
        else if (name == "pcv:")    EnsureService(name.c_str(), PcvInitialize);

        u32 session = IpcManager::Instance().Connect(name.c_str());
        LOG_INFO("SM: GetService('%s') → session 0x%x", name.c_str(), session);

        // Write output: 8 bytes (session handle + padding)
        if (*out_sz >= 8) {
            out[0] = session & 0xFF; out[1] = (session>>8) & 0xFF;
            out[2] = (session>>16) & 0xFF; out[3] = (session>>24) & 0xFF;
            out[4] = out[5] = out[6] = out[7] = 0;
            *out_sz = 8;
        }
        return true;
    }

    bool HandleRegisterService(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        LOG_DEBUG("SM: RegisterService");
        *out_sz = 0;
        return true;
    }

    // Lazy-init known services — auto-initialize on first access.
    // Each init function triggers the static constructor in the service's .cpp file.
    static void EnsureService(const char* name, void(*init)()) {
        static bool vi_init=false, nv_init=false, fs_init=false, hid_init=false;
        static bool set_init=false, apm_init=false, time_init=false, audio_init=false;
        static bool am_init=false, ns_init=false, ldr_init=false, spl_init=false;
        static bool acc_init=false, pcv_init=false;

        if (strcmp(name, "vi:") == 0 || strcmp(name, "vi:m") == 0) {
            if (!vi_init) { vi_init = true; ViInitialize(); }
        } else if (strcmp(name, "nvdrv:") == 0 || strcmp(name, "nvdrv#") == 0) {
            if (!nv_init) { nv_init = true; NvInitialize(); }
        } else if (strcmp(name, "fsp-srv:") == 0 || strcmp(name, "fs:") == 0) {
            if (!fs_init) { fs_init = true; FsInitialize(); }
        } else if (strcmp(name, "hid:") == 0) {
            if (!hid_init) { hid_init = true; HidInitialize(); }
        } else if (strcmp(name, "set:") == 0 || strcmp(name, "set:sys") == 0) {
            if (!set_init) { set_init = true; SetInitialize(); }
        } else if (strcmp(name, "apm:") == 0 || strcmp(name, "apm:sys") == 0) {
            if (!apm_init) { apm_init = true; ApmInitialize(); }
        } else if (strcmp(name, "time:") == 0 || strcmp(name, "time:a") == 0) {
            if (!time_init) { time_init = true; TimeInitialize(); }
        } else if (strcmp(name, "audout:") == 0 || strcmp(name, "audren:") == 0) {
            if (!audio_init) { audio_init = true; AudioOutInitialize(); }
        } else if (strcmp(name, "appletOE:") == 0 || strcmp(name, "appletAE:") == 0 || strcmp(name, "applet") == 0) {
            if (!am_init) { am_init = true; AmInitialize(); }
        } else if (strcmp(name, "ns:") == 0 || strcmp(name, "ns:dev") == 0 || strcmp(name, "ns:am2") == 0) {
            if (!ns_init) { ns_init = true; NsInitialize(); }
        } else if (strcmp(name, "ldr:") == 0 || strcmp(name, "ldr:pm") == 0) {
            if (!ldr_init) { ldr_init = true; LdrInitialize(); }
        } else if (strcmp(name, "spl:") == 0 || strcmp(name, "spl:mig") == 0 || strcmp(name, "spl:fs") == 0) {
            if (!spl_init) { spl_init = true; SplInitialize(); }
        } else if (strcmp(name, "acc:u0") == 0 || strcmp(name, "acc:u1") == 0 || strcmp(name, "acc:su") == 0) {
            if (!acc_init) { acc_init = true; AccountInitialize(); }
        } else if (strcmp(name, "pcv:") == 0) {
            if (!pcv_init) { pcv_init = true; PcvInitialize(); }
        }
    }

    static void ViInitialize();
    static void NvInitialize();
    static void FsInitialize();
    static void HidInitialize();
    static void SetInitialize();
    static void ApmInitialize();
    static void TimeInitialize();
    static void AmInitialize();
    static void NsInitialize();
    static void LdrInitialize();
    static void SplInitialize();
    static void AccountInitialize();
    static void PcvInitialize();
    static void AudioOutInitialize();
};

// ── Forward declarations (defined in Vi.cpp / Nv.cpp) ──────
void SmService::ViInitialize()  { extern void ServiceVi_Init();  ServiceVi_Init(); }
void SmService::NvInitialize()  { extern void ServiceNv_Init();  ServiceNv_Init(); }
void SmService::FsInitialize()  { extern void ServiceFs_Init();  ServiceFs_Init(); }
void SmService::HidInitialize() { extern void ServiceHid_Init(); ServiceHid_Init(); }
void SmService::SetInitialize() { extern void ServiceSet_Init(); ServiceSet_Init(); }
void SmService::ApmInitialize() { extern void ServiceApm_Init(); ServiceApm_Init(); }
void SmService::TimeInitialize(){ extern void ServiceTime_Init();ServiceTime_Init(); }
void SmService::AmInitialize()   { extern void ServiceAm_Init();   ServiceAm_Init(); }
void SmService::NsInitialize()   { extern void ServiceNs_Init();   ServiceNs_Init(); }
void SmService::LdrInitialize()  { extern void ServiceLdr_Init();  ServiceLdr_Init(); }
void SmService::SplInitialize()  { extern void ServiceSpl_Init();  ServiceSpl_Init(); }
void SmService::AccountInitialize(){extern void ServiceAccount_Init();ServiceAccount_Init();}
void SmService::PcvInitialize()  { extern void ServicePcv_Init();  ServicePcv_Init(); }
void SmService::AudioOutInitialize() { extern void ServiceAudioOut_Init(); ServiceAudioOut_Init(); }

// ── Global registration ─────────────────────────────────────
static SmService g_sm_service;

void ServiceSm_Init() {
    LOG_INFO("SM service ready");
    (void)g_sm_service;  // ensure static init runs
}

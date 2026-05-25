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
        // Pre-register known services
        IpcManager::Instance().RegisterService("sm:", this);
        IpcManager::Instance().RegisterService("vi:", nullptr);  // lazy init
        IpcManager::Instance().RegisterService("vi:m", nullptr);
        IpcManager::Instance().RegisterService("nvdrv:", nullptr);
        IpcManager::Instance().RegisterService("nvdrv#", nullptr);
        IpcManager::Instance().RegisterService("hid:", nullptr);
        IpcManager::Instance().RegisterService("set:", nullptr);
        IpcManager::Instance().RegisterService("apm:", nullptr);
        IpcManager::Instance().RegisterService("apm:sys", nullptr);
        IpcManager::Instance().RegisterService("time:", nullptr);
        IpcManager::Instance().RegisterService("time:a", nullptr);
        IpcManager::Instance().RegisterService("fsp-srv:", nullptr);
        IpcManager::Instance().RegisterService("fs:", nullptr);
        IpcManager::Instance().RegisterService("set:", nullptr);
        IpcManager::Instance().RegisterService("set:sys", nullptr);
        IpcManager::Instance().RegisterService("appletOE:", nullptr);
        IpcManager::Instance().RegisterService("appletAE:", nullptr);
        IpcManager::Instance().RegisterService("applet", nullptr);
        IpcManager::Instance().RegisterService("ns:", nullptr);
        IpcManager::Instance().RegisterService("ns:dev", nullptr);
        IpcManager::Instance().RegisterService("ns:am2", nullptr);
        IpcManager::Instance().RegisterService("ldr:", nullptr);
        IpcManager::Instance().RegisterService("ldr:pm", nullptr);
        IpcManager::Instance().RegisterService("spl:", nullptr);
        IpcManager::Instance().RegisterService("spl:mig", nullptr);
        IpcManager::Instance().RegisterService("acc:u0", nullptr);
        IpcManager::Instance().RegisterService("acc:u1", nullptr);
        IpcManager::Instance().RegisterService("pcv:", nullptr);
        IpcManager::Instance().RegisterService("psc:", nullptr);
        IpcManager::Instance().RegisterService("pm:", nullptr);
        IpcManager::Instance().RegisterService("nifm:", nullptr);
        IpcManager::Instance().RegisterService("nifm:oa", nullptr);
        IpcManager::Instance().RegisterService("ro:", nullptr);
        IpcManager::Instance().RegisterService("erpt:", nullptr);
        IpcManager::Instance().RegisterService("fatal:", nullptr);
        IpcManager::Instance().RegisterService("btm:", nullptr);
        IpcManager::Instance().RegisterService("nfc:", nullptr);

    }

    const char* Name() const override { return "sm:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            LOG_DEBUG("SM: Initialize");
            *out_sz = 0;
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

        // Register service if it's known but not yet registered
        if (name == "vi:")            EnsureService("vi:", ViInitialize);
        else if (name == "vi:m")      EnsureService("vi:m", ViInitialize);
        else if (name == "nvdrv:")    EnsureService("nvdrv:", NvInitialize);
        else if (name == "fsp-srv:" || name == "fs:")
                                      EnsureService("fsp-srv:", FsInitialize);
        else if (name == "hid:")      EnsureService("hid:", HidInitialize);
        else if (name == "set:")      EnsureService("set:", SetInitialize);
        else if (name == "apm:")      EnsureService("apm:", ApmInitialize);
        else if (name == "time:")     EnsureService("time:", TimeInitialize);

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

    // Lazy-init known services
    static void EnsureService(const char* name, void(*init)()) {
        static bool vi_init = false, nv_init = false;
        if (name == std::string("vi:") || name == std::string("vi:m")) {
            if (!vi_init) { vi_init = true; init(); }
        }
        if (name == std::string("nvdrv:")) {
            if (!nv_init) { nv_init = true; init(); }
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
    static void PscInitialize();
    static void PmInitialize();
    static void NifmInitialize();
    static void RoInitialize();
    static void ErptInitialize();
    static void FatalInitialize();
    static void BtmInitialize();
    static void NfcInitialize();
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
void SmService::PscInitialize()  { extern void ServicePsc_Init();  ServicePsc_Init(); }
void SmService::PmInitialize()   { extern void ServicePm_Init();   ServicePm_Init(); }
void SmService::NifmInitialize() { extern void ServiceNifm_Init(); ServiceNifm_Init(); }
void SmService::RoInitialize()   { extern void ServiceRo_Init();   ServiceRo_Init(); }
void SmService::ErptInitialize() { extern void ServiceErpt_Init(); ServiceErpt_Init(); }
void SmService::FatalInitialize(){ extern void ServiceFatal_Init();ServiceFatal_Init(); }
void SmService::BtmInitialize()  { extern void ServiceBtm_Init();  ServiceBtm_Init(); }
void SmService::NfcInitialize()  { extern void ServiceNfc_Init();  ServiceNfc_Init(); }

// ── Global registration ─────────────────────────────────────
static SmService g_sm_service;

void ServiceSm_Init() {
    LOG_INFO("SM service ready");
    (void)g_sm_service;  // ensure static init runs
}

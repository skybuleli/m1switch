#include "services/Ipc.h"
#include "common/Log.h"
#include <cstring>
#include <ctime>
#include <cstdlib>

// ── SPL (Security) Service ──────────────────────────────────
// Key derivation, random generation, secure storage.
// Games initialize this for crypto operations and random.

class SplService : public ServiceBase {
public:
    SplService() {
        IpcManager::Instance().RegisterService("spl:", this);
        IpcManager::Instance().RegisterService("spl:mig", this);
        IpcManager::Instance().RegisterService("spl:fs", this);
        std::srand((unsigned)std::time(nullptr));
    }

    const char* Name() const override { return "spl:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            *out_sz = 0; return true;

        case 5: // GenerateRandomBytes
            return HandleRandomBytes(in, in_sz, out, out_sz);

        case 7: // GetDeviceId
            if (*out_sz >= 16) {
                std::memset(out, 0, 16);
                out[0] = 0x01; out[1] = 0x23; out[2] = 0x45; out[3] = 0x67;
                *out_sz = 16;
            }
            return true;

        case 12: // GetSslKey
        case 13: // GetSslCertificate
        case 14: // GetEticketKey
        case 15: // GetStorageKey
        case 17: // GetAccessKey
        case 20: // DecryptTitleKey
        case 25: // SetBootReason
        case 26: // GetBootReason
        case 27: // GenerateAesKek
        case 28: // GenerateAesKey
        case 30: // DecryptAesKey
        case 31: // CryptAesCtr
        case 33: // GetPackage2Hash
            LOG_TRACE("SPL: stub cmd %u", cmd_id);
            if (*out_sz >= 16) {
                std::memset(out, 0, *out_sz);
            }
            return true;

        default:
            LOG_TRACE("SPL: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    bool HandleRandomBytes(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 count = 0;
        if (in_sz >= 4) std::memcpy(&count, in, 4);
        if (count == 0) count = 32;
        if (count > *out_sz) count = (u32)*out_sz;

        for (u32 i = 0; i < count; i++)
            out[i] = (u8)(std::rand() & 0xFF);

        *out_sz = count;
        LOG_DEBUG("SPL: GenerateRandomBytes(%u)", count);
        return true;
    }
};

static SplService g_spl_service;
void ServiceSpl_Init() { LOG_INFO("SPL service ready"); (void)g_spl_service; }

#include "services/Ipc.h"
#include "common/Log.h"
#include <cstring>

// ── Account Service ─────────────────────────────────────────
// User profile, user list, saved user data.

class AccountService : public ServiceBase {
public:
    AccountService() {
        IpcManager::Instance().RegisterService("acc:u0", this);
        IpcManager::Instance().RegisterService("acc:u1", this);
        IpcManager::Instance().RegisterService("acc:su", this);
    }

    const char* Name() const override { return "acc:u0"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (cmd_id) {
        case 0: // Initialize
            *out_sz = 0; return true;

        case 1: // GetUserCount
            if (*out_sz >= 4) {
                std::memset(out, 0, 4);
                out[0] = 1;  // 1 user
                *out_sz = 4;
            }
            return true;

        case 2: // GetUserExistence
            if (*out_sz >= 1) {
                out[0] = 1;  // User exists
                *out_sz = 1;
            }
            return true;

        case 100: // TrySelectUserWithoutInteraction
            if (*out_sz >= 8) {
                u64 uid = 1; // default user ID
                std::memcpy(out, &uid, 8);
                *out_sz = 8;
            }
            return true;

        case 3: // ListAllUsers
            if (*out_sz >= 0x10) {
                std::memset(out, 0, 0x10);
                // Return a dummy user ID (8 bytes)
                out[0] = 0x01;  // User ID
                *out_sz = 0x10;
            }
            return true;

        case 4: // GetProfileUrl
        case 5: // GetProfile
        case 6: // GetUserData
        case 101: // DeleteSaveDataId
        case 140: // GetSaveDataOwnerId
            if (*out_sz >= 8) { std::memset(out, 0, 8); *out_sz = 8; }
            return true;

        default:
            LOG_TRACE("Account: unhandled cmd %u", cmd_id);
            *out_sz = 0;
            return true;
        }
    }
};

static AccountService g_account_service;
void ServiceAccount_Init() { LOG_INFO("Account service ready"); (void)g_account_service; }

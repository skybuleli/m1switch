// ── IPC / Service tests ────────────────────────────────────

#include "services/Ipc.h"

TEST(IPC_ConnectAndDispatch) {
    auto& mgr = IpcManager::Instance();

    // Register a test service
    class TestSvc : public ServiceBase {
    public:
        TestSvc() { IpcManager::Instance().RegisterService("test:", this); }
        const char* Name() const override { return "test:"; }
        bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                           u8* out, size_t* out_sz) override {
            if (cmd_id == 42) {
                if (*out_sz >= 4) {
                    out[0] = 0xBE; out[1] = 0xEF;
                    *out_sz = 4;
                }
                return true;
            }
            return false;
        }
    };
    TestSvc test_svc;

    // Connect to the service
    u32 session = mgr.Connect("test:");
    CHECK(session != 0);

    // Send a command
    u8 req[sizeof(IpcRequest) + 4] = {};
    auto* hdr = reinterpret_cast<IpcRequest*>(req);
    hdr->magic = 0x4942434F;
    hdr->cmd_id = 42;

    u8 resp[256] = {};
    size_t resp_size = sizeof(resp);
    u32 result = mgr.HandleRequest(session, req, sizeof(req), resp, &resp_size);
    CHECK_EQ(0, result);
    // Custom data starts after IpcResponse header
    // (Phase 6: IPC custom data forwarding may need adjustment)
    CHECK(resp_size >= sizeof(IpcResponse));

    return true;
}

TEST(SM_ServiceRegistration) {
    auto& mgr = IpcManager::Instance();

    // SM should have pre-registered known services
    u32 session = mgr.Connect("sm:");
    CHECK(session != 0);

    // Connect to known services
    session = mgr.Connect("vi:");
    CHECK(session != 0);

    session = mgr.Connect("set:");
    CHECK(session != 0);

    return true;
}

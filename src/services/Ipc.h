#pragma once

#include "common/Types.h"
#include <span>
#include <functional>
#include <string>
#include <vector>

// ── Nintendo Switch IPC (tipc/cmif) Framework ──────────────
// Minimal implementation for Phase P0.

// IPC message types
enum class IpcCommandType : u32 {
    Invalid    = 0,
    LegacyRequest  = 1,
    Close        = 2,
    LegacyControl = 3,
    Request      = 4,
    Control      = 5,
    RequestWithContext = 6,
    ControlWithContext = 7,
};

// IPC header (CMIF)
struct IpcHeader {
    u32 type;              // IpcCommandType
    u32 send_count;        // Number of send buffers
    u32 recv_count;        // Number of receive buffers
    u32 raw_data_size;     // Inline data size (bytes)
    u32 pad0;
    u64 pid;               // Process ID (0 for P0)
    u32 copy_handles;
    u32 move_handles;
};

// ── Service interface ──────────────────────────────────────
// Each service handles a specific domain.

class ServiceBase {
public:
    virtual ~ServiceBase() = default;
    virtual const char* Name() const = 0;

    // Handle an IPC command. Returns true if handled.
    // @param cmd_id    Command ID (CMIF function number)
    // @param in_data   Input raw data pointer
    // @param in_size   Input data size
    // @param out_data  [out] Output data buffer
    // @param out_size  [in/out] Output buffer size / written size
    virtual bool HandleCommand(u32 cmd_id, const u8* in_data, size_t in_size,
                               u8* out_data, size_t* out_size) = 0;
};

// ── Session / Service Manager ──────────────────────────────
// Manages sessions created via svcConnectToNamedPort.
// Routes SendSyncRequest to the correct service.

class IpcManager {
public:
    IpcManager();
    ~IpcManager();

    // Register a service
    void RegisterService(const char* name, ServiceBase* service);

    // Look up service by name (called from svcConnectToNamedPort)
    // Returns a session handle (0 = invalid)
    u32 Connect(const char* name);

    // Handle an IPC request (called from svcSendSyncRequest)
    // @param session  Session handle from Connect()
    // @param data     IPC message data
    // @param size     Message size
    // @param response [out] Response buffer
    // @param resp_size [in/out] Buffer size / response size
    // Returns 0 on success
    u32 HandleRequest(u32 session, const u8* data, size_t size,
                      u8* response, size_t* resp_size);

    // Get the singleton instance
    static IpcManager& Instance();

private:
    struct Session {
        u32 id;
        std::string service_name;
        ServiceBase* service = nullptr;
    };

    std::vector<Session> sessions_;
    u32 next_session_ = 0xCAFE0001;
};

// ── Convenience: define a service's command table ──────────
#define SERVICE_CMD(cmd, fn) \
    if (cmd_id == cmd) { fn(in_data, in_size, out_data, out_size); return true; }

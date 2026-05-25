#pragma once

#include "common/Types.h"
#include <span>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <functional>

// ── Nintendo Switch IPC (tipc/cmif) Framework ──────────────

// CMIF request header
struct IpcRequest {
    u32 magic;       // 0x4942434F ("IBCF")
    u32 cmd_id;      // Command ID
    u32 pad0;
    u32 pad1;
    u64 pid;         // Client PID (0 for P0)
    u32 copy_handles;
    u32 move_handles;
};

// CMIF response header
struct IpcResponse {
    u32 magic;       // 0x4942434F ("OCBI")
    u32 result;      // 0 = success
    u32 pad0;
    u32 pad1;
};

// ── Service interface ──────────────────────────────────────
class ServiceBase {
public:
    virtual ~ServiceBase() = default;
    virtual const char* Name() const = 0;

    // Handle a command.
    // @param cmd_id     Command ID
    // @param in_data    Input raw data
    // @param in_size    Input data size
    // @param out_data   [out] Output data buffer
    // @param out_size   [in/out] Max output size / written size
    virtual bool HandleCommand(u32 cmd_id, const u8* in_data, size_t in_size,
                               u8* out_data, size_t* out_size) = 0;
};

// ── IpcManager ────────────────────────────────────────────
class IpcManager {
public:
    IpcManager();
    ~IpcManager();

    void RegisterService(const char* name, ServiceBase* service);
    u32  Connect(const char* name);
    u32  HandleRequest(u32 session, const u8* data, size_t size,
                       u8* response, size_t* resp_size);

    static IpcManager& Instance();

private:
    struct Session {
        u32 id;
        std::string service_name;
        ServiceBase* service = nullptr;
    };
    std::unordered_map<std::string, ServiceBase*> services_;
    std::vector<Session> sessions_;
    u32 next_session_ = 0xCAFE0001;
    std::mutex mutex_;
};

// ── Helper ─────────────────────────────────────────────────
#define SERVICE_CMD(cmd, fn) \
    if (cmd_id == cmd) { fn(in_data, in_size, out_data, out_size); return true; }

// Write a u32 to output at offset, advance out_size
inline void IpcWriteU32(u8* out, size_t off, u32 val) {
    out[off+0] = (val>>0)&0xFF; out[off+1] = (val>>8)&0xFF;
    out[off+2] = (val>>16)&0xFF; out[off+3] = (val>>24)&0xFF;
}

inline u32 IpcReadU32(const u8* data, size_t off) {
    return (u32)data[off+0] | ((u32)data[off+1]<<8) |
           ((u32)data[off+2]<<16) | ((u32)data[off+3]<<24);
}

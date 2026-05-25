#include "services/Ipc.h"
#include "common/Log.h"

#include <cstring>
#include <algorithm>

IpcManager::IpcManager() {}
IpcManager::~IpcManager() {}

IpcManager& IpcManager::Instance() {
    static IpcManager inst;
    return inst;
}

void IpcManager::RegisterService(const char* name, ServiceBase* service) {
    LOG_INFO("IPC: service '%s' registered", name);
}

u32 IpcManager::Connect(const char* name) {
    // Find matching service
    // Phase P0: dynamic lookup not implemented yet, return dummy session
    LOG_DEBUG("IPC: Connect('%s') → session 0x%x", name, next_session_);
    Session s;
    s.id = next_session_++;
    s.service_name = name;
    s.service = nullptr;  // Phase P0: stub
    sessions_.push_back(s);
    return s.id;
}

u32 IpcManager::HandleRequest(u32 session, const u8* data, size_t size,
                               u8* response, size_t* resp_size) {
    LOG_DEBUG("IPC: HandleRequest(session=0x%x, size=%zu)", session, size);

    // Phase P0: parse CMIF header, echo success
    if (size < sizeof(IpcHeader)) {
        LOG_WARN("IPC: message too small (%zu)", size);
        return 0xFFFF;  // Error
    }

    // Parse header
    const IpcHeader* hdr = reinterpret_cast<const IpcHeader*>(data);
    u32 cmd_id = hdr->type;

    LOG_DEBUG("IPC: cmd_type=%u, raw_size=%u", cmd_id, hdr->raw_data_size);

    // Write a minimal CMIF response
    IpcHeader* resp = reinterpret_cast<IpcHeader*>(response);
    memset(resp, 0, sizeof(IpcHeader));
    resp->type = 0;  // Success
    *resp_size = sizeof(IpcHeader);

    return 0;  // Success
}

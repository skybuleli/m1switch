#include "services/Ipc.h"
#include "common/Log.h"
#include "debug/TraceEngine.h"
#include <cstring>
#include <algorithm>

IpcManager::IpcManager() {}
IpcManager::~IpcManager() {}

IpcManager& IpcManager::Instance() {
    static IpcManager inst;
    return inst;
}

void IpcManager::RegisterService(const char* name, ServiceBase* service) {
    std::lock_guard<std::mutex> lock(mutex_);
    services_[name] = service;
    LOG_INFO("IPC: service '%s' registered @ %p", name, (void*)service);
}

u32 IpcManager::Connect(const char* name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = services_.find(name);
    ServiceBase* svc = (it != services_.end()) ? it->second : nullptr;

    u32 sid = next_session_++;
    Session s;
    s.id = sid;
    s.service_name = name;
    s.service = svc;
    sessions_.push_back(s);

    LOG_DEBUG("IPC: Connect('%s') → session 0x%x (svc=%p)", name, sid, (void*)svc);
    return sid;
}

u32 IpcManager::CreateSession(ServiceBase* service) {
    std::lock_guard<std::mutex> lock(mutex_);
    u32 sid = next_session_++;
    Session s;
    s.id = sid;
    s.service_name = "(anonymous)";
    s.service = service;
    sessions_.push_back(s);
    LOG_DEBUG("IPC: CreateSession → session 0x%x (anonymous, svc=%p)", sid, (void*)service);
    return sid;
}

u32 IpcManager::HandleRequest(u32 session_handle, const u8* data, size_t size,
                               u8* response, size_t* resp_size) {
    // Find session
    Session* session = nullptr;
    for (auto& s : sessions_) {
        if (s.id == session_handle) { session = &s; break; }
    }

    if (!session) {
        LOG_WARN("IPC: invalid session 0x%x", session_handle);
        return 0xFFFF;
    }

    if (size < sizeof(IpcRequest)) {
        LOG_WARN("IPC: message too small (%zu)", size);
        return 0xFFFF;
    }

    const IpcRequest* req = reinterpret_cast<const IpcRequest*>(data);

    // Build response
    IpcResponse* resp = reinterpret_cast<IpcResponse*>(response);
    memset(resp, 0, sizeof(IpcResponse));
    resp->magic = 0x4F434653;  // "SFCO" in LE
    resp->result = 0;

    // Save total buffer size before overwriting
    size_t total_buf_size = *resp_size;
    *resp_size = sizeof(IpcResponse);

if (session->service) {
        // Raw data is after the header
        const u8* raw_in = data + sizeof(IpcRequest);
        size_t raw_in_size = (size > sizeof(IpcRequest)) ? size - sizeof(IpcRequest) : 0;
        u8* raw_out = response + sizeof(IpcResponse);
        size_t raw_out_max = (total_buf_size > sizeof(IpcResponse))
                            ? total_buf_size - sizeof(IpcResponse) : 0;

        // ── IPC 追踪：记录请求 ──────────────────────────────
        TRACE_IPC(req->cmd_id,
                   (u64)session_handle,
                   raw_in_size > 0 ? *((const u64*)raw_in) : 0,
                   0);

        bool handled = session->service->HandleCommand(
            req->cmd_id, raw_in, raw_in_size, raw_out, &raw_out_max);

        if (handled) {
            resp->result = 0;
            *resp_size = sizeof(IpcResponse) + raw_out_max;
        } else {
            LOG_WARN("IPC: unhandled cmd %u for '%s'",
                     req->cmd_id, session->service_name.c_str());
            resp->result = 1;  // Unhandled
            *resp_size = sizeof(IpcResponse);
        }

        // ── IPC 追踪：记录响应 ──────────────────────────────
        TRACE_IPC(req->cmd_id | 0x8000,
                  (u64)session_handle,
                  (u64)resp->result,
                  (u64)handled);
    } else {
        LOG_TRACE("IPC: session 0x%x ('%s') has no handler", session_handle,
                 session->service_name.c_str());
        *resp_size = sizeof(IpcResponse);
    }

    return resp->result;
}

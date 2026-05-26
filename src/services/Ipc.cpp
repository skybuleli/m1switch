#include "services/Ipc.h"
#include "common/Log.h"
#include "debug/TraceEngine.h"
#include <cstring>
#include <algorithm>

// ── hipc 协议结构（匹配 libnx 定义）─────────────────────
struct __attribute__((packed)) HipcHeader {
    u32 type               : 16;
    u32 num_send_statics   : 4;
    u32 num_send_buffers   : 4;
    u32 num_recv_buffers   : 4;
    u32 num_exch_buffers   : 4;
    u32 num_data_words     : 10;
    u32 recv_static_mode   : 4;
    u32 padding            : 6;
    u32 recv_list_offset   : 11;
    u32 has_special_header : 1;
};
static_assert(sizeof(HipcHeader) == 8, "HipcHeader must be 8 bytes");

struct __attribute__((packed)) HipcSpecialHeader {
    u32 send_pid         : 1;
    u32 num_copy_handles : 4;
    u32 num_move_handles : 4;
    u32 padding          : 23;
};
static_assert(sizeof(HipcSpecialHeader) == 4, "HipcSpecialHeader must be 4 bytes");

// 从 hipc 请求缓冲区中定位 data_words 的偏移
static size_t CalcDataWordsOffset(const u8* data, size_t size) {
    if (size < sizeof(HipcHeader)) return 0;
    HipcHeader hdr;
    std::memcpy(&hdr, data, sizeof(hdr));

    size_t off = sizeof(HipcHeader);

    // Special header + optional PID
    if (hdr.has_special_header) {
        if (off + sizeof(HipcSpecialHeader) > size) return 0;
        HipcSpecialHeader sh;
        std::memcpy(&sh, data + off, sizeof(sh));
        off += sizeof(HipcSpecialHeader);
        if (sh.send_pid) off += 8;  // skip PID (u64)
        // skip copy handles
        off += sh.num_copy_handles * sizeof(u32);
        // skip move handles
        off += sh.num_move_handles * sizeof(u32);
    }

    // 静态描述符: sizeof = 8 (HipcStaticDescriptor)
    off += hdr.num_send_statics * 8;
    // Buffer descriptors: sizeof = 16 (HipcBufferDescriptor)
    off += (hdr.num_send_buffers + hdr.num_recv_buffers + hdr.num_exch_buffers) * 16;

    return off < size ? off : 0;
}

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
    // ── 查找 session ────────────────────────────────────
    Session* session = nullptr;
    for (auto& s : sessions_) {
        if (s.id == session_handle) { session = &s; break; }
    }
    if (!session) { LOG_WARN("IPC: invalid session 0x%x", session_handle); return 0xFFFF; }
    if (size < sizeof(HipcHeader)) { LOG_WARN("IPC: msg too small %zu", size); return 0xFFFF; }

    // ── 解析 hipc 请求 ───────────────────────────────────
    HipcHeader req_hdr;
    std::memcpy(&req_hdr, data, sizeof(req_hdr));
    size_t dw_off = CalcDataWordsOffset(data, size);

    // 从 data_words 中提取命令 ID
    u32 cmd_id = 0;
    if (dw_off > 0 && req_hdr.num_data_words > 0) {
        std::memcpy(&cmd_id, data + dw_off, sizeof(cmd_id));
    }

    // raw_in = data_words 之后的数据（如果有多个 data_words 或 buffer 数据）
    const u8* raw_in = data + dw_off + (req_hdr.num_data_words * sizeof(u32));
    size_t raw_in_size = (raw_in > data && raw_in < data + size) ? (data + size) - raw_in : 0;
    // 如果只有一个 data_word（命令 ID），则没有额外的 raw_in 数据
    if (req_hdr.num_data_words <= 1) { raw_in = nullptr; raw_in_size = 0; }

    // ── 构建 hipc 响应（空：仅 HipcHeader）───────────────
    HipcHeader resp_hdr = {};
    resp_hdr.type = 0;  // 成功
    // 对于需要返回数据的命令，服务应填充 raw_out
    u8* raw_out = response + sizeof(HipcHeader);
    size_t raw_out_max = (*resp_size > sizeof(HipcHeader)) ? *resp_size - sizeof(HipcHeader) : 0;

    // 默认响应大小 = HipcHeader
    *resp_size = sizeof(HipcHeader);

    if (!session->service) {
        LOG_TRACE("IPC: session 0x%x ('%s') no handler", session_handle, session->service_name.c_str());
        return 0;
    }

    // ── IPC 追踪：请求 ────────────────────────────────────
    TRACE_IPC(cmd_id, (u64)session_handle, raw_in_size > 0 ? *((const u64*)raw_in) : 0, 0);

    // ── 调用服务 ─────────────────────────────────────────
    bool handled = session->service->HandleCommand(cmd_id, raw_in, raw_in_size, raw_out, &raw_out_max);

    if (handled) {
        // 如果服务有输出数据，将其作为 data_words 附加到 HipcHeader 后
        if (raw_out_max > 0) {
            *resp_size = sizeof(HipcHeader) + raw_out_max;
            resp_hdr.num_data_words = (raw_out_max + 3) / 4;  // 按 u32 对齐
        }
    } else {
        LOG_WARN("IPC: unhandled cmd %u for '%s'", cmd_id, session->service_name.c_str());
        // 返回错误——libnx 检查 SVC result (x0)，不是 hipc 头
    }

    // 写入 HipcHeader
    std::memcpy(response, &resp_hdr, sizeof(HipcHeader));

    // ── IPC 追踪：响应 ────────────────────────────────────
    TRACE_IPC(cmd_id | 0x8000, (u64)session_handle, (u64)handled, 0);

    return 0;  // 成功（SVC result = x0）
}

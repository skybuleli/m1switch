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

    // 调试：输出 IPC 请求 full hex（32 bytes）
    LOG_DEBUG("IPC REQ session=0x%x cmd=%u hex=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
              session_handle, cmd_id,
              data[0],data[1],data[2],data[3],
              data[4],data[5],data[6],data[7],
              data[8],data[9],data[10],data[11],
              data[12],data[13],data[14],data[15],
              data[16],data[17],data[18],data[19],
              data[20],data[21],data[22],data[23],
              data[24],data[25],data[26],data[27],
              data[28],data[29],data[30],data[31]);

    // raw_in = data_words 之后的数据（如果有多个 data_words 或 buffer 数据）
    const u8* raw_in = data + dw_off + (req_hdr.num_data_words * sizeof(u32));
    size_t raw_in_size = (raw_in > data && raw_in < data + size) ? (data + size) - raw_in : 0;
    // 如果只有一个 data_word（命令 ID），则没有额外的 raw_in 数据
    if (req_hdr.num_data_words <= 1) { raw_in = nullptr; raw_in_size = 0; }

    // ── 构建 hipc 响应 ──────────────────────────────────
    HipcHeader resp_hdr = {};
    resp_hdr.type = 0;

    // 预留空间：HipcHeader + 可选的 SpecialHeader(4) + 最多 2 个句柄(8)
    u8* resp_ptr = response;
    size_t resp_remaining = *resp_size;

    // 跳过 HipcHeader（稍后写入）
    if (resp_remaining < sizeof(HipcHeader)) { *resp_size = 0; return 0; }
    resp_ptr += sizeof(HipcHeader);
    resp_remaining -= sizeof(HipcHeader);

    // Special header + handle 区域（先预留，按需填充）
    u8* shdr_pos = nullptr;
    u8* handle_pos = nullptr;
    u32 num_copy_handles = 0;
    u32 num_move_handles = 0;

    // 对于 SM::Initialize (cmd_id=0)，libnx 期望输出句柄
    // 但由于句柄值需要有效内核对象支持，暂时不返回句柄，仅返回 data_words
    if (false && session && session->service_name == "sm:" && cmd_id == 0) {
        if (resp_remaining >= 4 + 4) { // SpecialHeader + 1 handle
            shdr_pos = resp_ptr;
            handle_pos = resp_ptr + 4; // after special header
            num_copy_handles = 1;
            // 写入特殊句柄值
            u32 dummy_handle = 0xCAFE0002;
            std::memcpy(handle_pos, &dummy_handle, sizeof(dummy_handle));
            resp_hdr.has_special_header = 1;
            resp_ptr += 4 + 4; // special header + 1 handle
            resp_remaining -= 4 + 4;
        }
    }

    // raw_out 在 HipcHeader + 可选句柄之后
    u8* raw_out = resp_ptr;
    size_t raw_out_max = resp_remaining;

    *resp_size = (size_t)(raw_out - response); // 基础大小 = header + 可选句柄

    if (!session->service) {
        LOG_TRACE("IPC: session 0x%x ('%s') no handler", session_handle, session->service_name.c_str());
        return 0;
    }

    // ── IPC 追踪：请求 ────────────────────────────────────
    TRACE_IPC(cmd_id, (u64)session_handle, raw_in_size > 0 ? *((const u64*)raw_in) : 0, 0);

    // ── 调用服务 ─────────────────────────────────────────
    bool handled = session->service->HandleCommand(cmd_id, raw_in, raw_in_size, raw_out, &raw_out_max);

    if (handled) {
        // 计算总响应大小
        size_t header_size = (size_t)(raw_out - response); // headeroffset
        size_t total_size = header_size + raw_out_max;
        
        // 写入 SpecialHeader（如果有句柄）
        if (shdr_pos) {
            HipcSpecialHeader shdr = {};
            shdr.num_copy_handles = num_copy_handles;
            shdr.num_move_handles = num_move_handles;
            std::memcpy(shdr_pos, &shdr, sizeof(shdr));
            // 设置 data_words 在句柄之后
            resp_hdr.num_data_words = (raw_out_max + 3) / 4;
        } else if (raw_out_max > 0) {
            resp_hdr.num_data_words = (raw_out_max + 3) / 4;
        }
        
        *resp_size = total_size;
    } else {
        LOG_WARN("IPC: unhandled cmd %u for '%s'", cmd_id, session->service_name.c_str());
        // 返回错误——libnx 检查 SVC result (x0)，不是 hipc 头
    }

    // 写入 HipcHeader
    std::memcpy(response, &resp_hdr, sizeof(HipcHeader));

    // 调试：输出 IPC 响应 hex dump
    LOG_DEBUG("IPC RESP cmd=%u session=0x%x sz=%zu hex=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
              cmd_id, session_handle, *resp_size,
              response[0],response[1],response[2],response[3],
              response[4],response[5],response[6],response[7],
              response[8],response[9],response[10],response[11],
              response[12],response[13],response[14],response[15]);

    // ── IPC 追踪：响应 ────────────────────────────────────
    TRACE_IPC(cmd_id | 0x8000, (u64)session_handle, (u64)handled, 0);

    return 0;  // 成功（SVC result = x0）
}

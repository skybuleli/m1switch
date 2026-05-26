#include "services/Ipc.h"
#include "common/Log.h"
#include "debug/TraceEngine.h"
#include "kernel/Kernel.h"
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

    // SM::Initialize 需要返回输出句柄（libnx 的 SfOutHandleAttr_HipcCopy）
    // 使用内核句柄表创建一个通用对象并返回其句柄
    if (session && session->service_name == "sm:" && cmd_id == 0) {
        if (resp_remaining >= 4 + 4) {
            // libnx 期望 SM 初始化返回服务进程句柄
            // 使用 KSession 更接近真实 SM（vs KEvent）
            KSession* sess = new KSession();
            sess->client_handle = session_handle;  // 关联此会话
            u32 kernel_handle = KernelHandleTable().Create(sess);
            LOG_DEBUG("SM: created KSession handle 0x%x for Initialize", kernel_handle);

            shdr_pos = resp_ptr;
            handle_pos = resp_ptr + 4;
            num_copy_handles = 1;
            std::memcpy(handle_pos, &kernel_handle, sizeof(kernel_handle));
            resp_hdr.has_special_header = 1;
            resp_ptr += 4 + 4;
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

    // 所有 CMIF 响应都需要以 CmifOutHeader{magic=SFCO, result=0} 开头
    // libnx 的 cmifParseResponse 强制检查此 magic
    u32 cmif_magic = 0x4F434653; // "SFCO" 
    u32 cmif_result = 0;           // 成功
    
    if (handled) {
        size_t header_size = (size_t)(raw_out - response);
        
        if (raw_out_max > 0) {
            // 服务返回了实际数据 → 需要 CMIF 封装：
            // 在 data_words 开头插入 CmifOutHeader{magic=SFCO, result=0}
            // 将服务输出向后移 8 字节腾出空间
            memmove(raw_out + 8, raw_out, raw_out_max);
            std::memcpy(raw_out, &cmif_magic, 4);
            std::memcpy(raw_out + 4, &cmif_result, 4);
            
            u32 total_out_size = 8 + raw_out_max;
            resp_hdr.num_data_words = (total_out_size + 3) / 4;
        } else {
            // 只有句柄没有数据（如 SM::Initialize）→ 不插入 CMIF 头
            // libnx 使用原地 IPC 缓冲，额外写入会污染下次请求
            resp_hdr.num_data_words = 0;
        }
        
        size_t total_out_size = raw_out_max > 0 ? (8 + raw_out_max) : 0;
        size_t total_size = header_size + total_out_size;
        
        if (shdr_pos) {
            HipcSpecialHeader shdr = {};
            shdr.num_copy_handles = num_copy_handles;
            shdr.num_move_handles = num_move_handles;
            std::memcpy(shdr_pos, &shdr, sizeof(shdr));
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

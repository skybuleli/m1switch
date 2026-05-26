#include "services/Ipc.h"
#include "common/Log.h"
#include "debug/TraceEngine.h"
#include "kernel/Kernel.h"
#include "memory/Memory.h"
#include <cstring>
#include <algorithm>

// SvcHandlers.cpp 中的 guest 内存指针，用于 recv_list 缓冲写入
extern Memory* g_mem;

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
// 注意: 某些 libnx 版本使用相反的位域顺序 (copy↔move)
// 我们定义两种以便验证。
struct __attribute__((packed)) HipcSpecialHeaderAlt {
    u32 send_pid         : 1;
    u32 num_move_handles : 4;  // swapped order
    u32 num_copy_handles : 4;
    u32 padding          : 23;
};
static_assert(sizeof(HipcSpecialHeader) == 4, "HipcSpecialHeader must be 4 bytes");

struct __attribute__((packed)) HipcRecvListEntry {
    u32 address_low;
    u32 address_high : 16;
    u32 size         : 16;
};
static_assert(sizeof(HipcRecvListEntry) == 8, "HipcRecvListEntry must be 8 bytes");

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

// 解析 hipc 请求中的 recv_list（输出缓冲描述符）
// recv_static_mode: 0=无, 2=自动, >2=条目数+2
// recv_list 位于 data_words 之后（在请求缓冲的末尾）
struct ParsedRecvList {
    size_t count = 0;
    struct Entry { u64 guest_addr; u32 size; } entries[8];
};
static ParsedRecvList ParseRecvList(const u8* data, const HipcHeader& hdr, size_t dw_off) {
    ParsedRecvList result;
    if (hdr.recv_static_mode == 0) return result;
    
    // 计算 recv_list 条目数量
    size_t num_entries = 0;
    if (hdr.recv_static_mode == 2) {
        num_entries = 8; // 保守处理
    } else if (hdr.recv_static_mode > 2) {
        num_entries = hdr.recv_static_mode - 2;
    }
    if (num_entries == 0) return result;
    
    // recv_list 位于 data_words 之后
    // recv_list_offset 是相对于 HipcHeader 的 u32 偏移
    size_t rl_off = dw_off + hdr.num_data_words * sizeof(u32);
    // 如果 recv_list_offset 在 header 中非零，使用它
    if (hdr.recv_list_offset > 0) {
        rl_off = hdr.recv_list_offset * sizeof(u32);
    }
    
    // 解析 HipcRecvListEntry 数组
    for (size_t i = 0; i < num_entries && i < 8; i++) {
        HipcRecvListEntry entry;
        std::memcpy(&entry, data + rl_off + i * sizeof(HipcRecvListEntry), sizeof(entry));
        u32 raw_addr = entry.address_low;
        // address_high 是 16 位，与 address_low 组合为 48 位地址
        u64 addr = (u64)raw_addr | ((u64)entry.address_high << 32);
        u32 size = entry.size;
        if (size > 0) {
            result.entries[result.count].guest_addr = addr;
            result.entries[result.count].size = size;
            result.count++;
        }
    }
    return result;
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
    
    // libnx 请求服务名可能带冒号或不带冒号，兼容两者
    ServiceBase* svc = nullptr;
    auto it = services_.find(name);
    if (it != services_.end()) {
        svc = it->second;
    } else {
        // 尝试带冒号: "apm" → "apm:"
        std::string with_colon = std::string(name) + ":";
        auto it2 = services_.find(with_colon);
        if (it2 != services_.end()) svc = it2->second;
    }

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

// 服务 Helper：创建并返回子会话句柄（用于 OpenSession 等服务）
u32 IpcManager::OpenSessionFor(const char* service_name) {
    auto it = services_.find(service_name);
    ServiceBase* svc = (it != services_.end()) ? it->second : nullptr;
    if (!svc) {
        LOG_WARN("IPC: OpenSessionFor('%s') - service not registered", service_name);
        return 0;
    }
    return CreateSession(svc);
}

// ── CMIF 协议常量 ─────────────────────────────────────
static constexpr u32 CMIF_IN_MAGIC  = 0x49434653; // "SFCI"
static constexpr u32 CMIF_OUT_MAGIC = 0x4F434653; // "SFCO"

// CMIF InHeader (16 bytes after 16-byte alignment)
struct CmifInHeader { u32 magic, version, command_id, token; };

// CMIF OutHeader (16 bytes after 16-byte alignment)
struct CmifOutHeader { u32 magic, version, result, token; };

// 计算 CMIF data 的对齐起始偏移（相对于 data_words 缓冲区的 16 字节对齐）
static size_t CalcCmifAlign(size_t dw_off) {
    return (dw_off + 15) & ~15;
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

    // 检测请求是否被上一次响应污染（原地 IPC 缓冲复用）
    bool recycled = false;
    if (req_hdr.has_special_header && size >= sizeof(HipcHeader) + 4) {
        HipcSpecialHeader sh;
        std::memcpy(&sh, data + sizeof(HipcHeader), sizeof(sh));
        if (sh.num_copy_handles > 0 || sh.num_move_handles > 0) {
            recycled = true;
        }
    }
    // 检测 SFCO magic（CMIF 响应头）出现在 data_words 中，表明请求是上一轮响应
    if (!recycled && req_hdr.num_data_words > 0) {
        size_t check_off = CalcCmifAlign(dw_off);
        if (check_off + 4 <= size) {
            u32 magic = 0;
            std::memcpy(&magic, data + check_off, sizeof(magic));
            if (magic == CMIF_OUT_MAGIC) { // "SFCO" 出现在请求中 = 回收响应
                recycled = true;
            }
        }
    }
    if (recycled) {
        LOG_DEBUG("IPC: detected recycled response buffer, resetting request");
        req_hdr = {};
        dw_off = sizeof(HipcHeader);
    }

    // ── 解析 recv_list（输出缓冲描述符）───────────────
    ParsedRecvList recv_list = ParseRecvList(data, req_hdr, dw_off);

    // ── CMIF 请求解析 ─────────────────────────────────
    // domain 模式: CmifInHeader 在 data_words + 4 处（无 16 字节对齐）
    // 非 domain: CmifInHeader 在 16 字节对齐处
    size_t cmif_off = session->is_domain ? (dw_off + 4) : CalcCmifAlign(dw_off);
    u32 cmd_id = 0;
    const u8* raw_in = nullptr;
    size_t raw_in_size = 0;

    u32 cmif_token = 0;

    // 如果 session 是 domain 模式，第一个 data_word 是 object_id，跳过它
    size_t domain_skip = session->is_domain ? 4 : 0;
    if (req_hdr.num_data_words > 0) {
        // 尝试解析 CmifInHeader
        CmifInHeader cmif_in = {};
        std::memcpy(&cmif_in, data + cmif_off, sizeof(cmif_in));

        if (cmif_in.magic == CMIF_IN_MAGIC) {
            // ✅ CMIF 格式请求
            cmd_id = cmif_in.command_id;
            cmif_token = cmif_in.token; // 保存 token 用于响应
            // raw_in 指向 CmifInHeader 之后的数据
            size_t cmif_end = cmif_off + sizeof(CmifInHeader);
            size_t dw_end = dw_off + req_hdr.num_data_words * sizeof(u32);
            if (cmif_end < dw_end) {
                raw_in = data + cmif_end;
                raw_in_size = dw_end - cmif_end;
            }
        } else if (dw_off > 0) {
            // ❌ 非 CMIF 格式（旧式/原始 hipc），从第一个 data_word 读取 cmd_id
            std::memcpy(&cmd_id, data + dw_off, sizeof(cmd_id));
            raw_in_size = 0;
        }
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

    // ── 构建 hipc 响应 ──────────────────────────────────
    HipcHeader resp_hdr = {};
    resp_hdr.type = 0;

    // 预留空间：HipcHeader + SpecialHeader(4) + 句柄(4-8)
    u8* resp_ptr = response;
    size_t resp_remaining = *resp_size;

    if (resp_remaining < sizeof(HipcHeader)) { *resp_size = 0; return 0; }
    resp_ptr += sizeof(HipcHeader);
    resp_remaining -= sizeof(HipcHeader);

    u8* shdr_pos = nullptr;
    u8* handle_pos = nullptr;
    u32 num_copy_handles = 0;
    u32 num_move_handles = 0;

    // 保存服务名（session 指针可能在 CreateSession 后被失效）
    std::string svc_name = session->service_name;
    const char* svc_name_cstr = svc_name.c_str();

    // 标记需要从服务输出中提取 move/copy handle 的情形
    bool needs_move_handle = false;
    bool needs_copy_handle = false;  // HidAppletResource 需要 copy handle
    std::string sname = svc_name;
    if (sname.back() == ':') sname.pop_back();
    if (sname == "sm" && cmd_id == 1) {
        needs_move_handle = true;
    } else if ((sname == "apm" || sname == "apm:sys") && cmd_id == 0) {
        needs_move_handle = true;
    } else if ((sname == "appletOE" || sname == "appletAE") && cmd_id == 0) {
        needs_move_handle = true;
    } else if ((sname == "hid" || sname == "hid:") && (cmd_id == 0 || cmd_id == 1)) {
        // cmd 0: _hidCreateAppletResource → 返回子会话 (move handle)
        // cmd 1: GetSharedMemory → 返回共享内存handle
        needs_move_handle = true;
    } else if (cmd_id <= 1000 && session && session->service &&
               strcmp(session->service->Name(), "appletProxy") == 0) {
        // applet 代理会话的所有子对象命令都返回 move handle
        needs_move_handle = true;
    } else if (session && session->service &&
               strcmp(session->service->Name(), "HidAppletResource") == 0) {
        // HID 子会话 cmd=0 (GetSharedMemoryHandle): 返回 KSharedMemory copy handle。
        // 使用 copy handle 而非 move handle（libnx 从 ipcOut->Handles[0] 读取）
        needs_move_handle = true;
        needs_copy_handle = true;
    }

    // SM::Initialize (cmd=0): 返回会话本身的句柄作为 copy handle
    if (sname == "sm" && cmd_id == 0) {
        if (resp_remaining >= 4 + 4) {
            LOG_DEBUG("SM: Initialize returning session_handle 0x%x", session_handle);
            shdr_pos = resp_ptr;
            handle_pos = resp_ptr + 4;
            num_copy_handles = 1;
            std::memcpy(handle_pos, &session_handle, sizeof(session_handle));
            resp_hdr.has_special_header = 1;
            resp_ptr += 4 + 4;
            resp_remaining -= 4 + 4;
        }
    }

    // 预分配 move/copy handle 空间: 服务将 handle 写入 raw_out[0..3] 后，
    // Ipc 层会将其提取到此处的 handle_pos。
    // 仅对普通请求(type=4)有效，控制命令(type=5 如 ConvertCurrentObjectToDomain)
    // 不应提取 move handle，否则 domain_id 会被误提取为句柄。
    bool is_control = (req_hdr.type == 5);
    if (needs_move_handle && !is_control && resp_remaining >= 4 + 4) {
        shdr_pos = resp_ptr;
        handle_pos = resp_ptr + 4;
        if (needs_copy_handle) {
            num_copy_handles = 1;
        } else {
            num_move_handles = 1;
        }
        resp_hdr.has_special_header = 1;
        resp_ptr += 4 + 4;
        resp_remaining -= 4 + 4;
    }

    // raw_out 指向响应 data_words 起始位置
    u8* raw_out = resp_ptr;
    size_t raw_out_max = resp_remaining;

    *resp_size = (size_t)(raw_out - response);

    if (!session->service) {
        LOG_TRACE("IPC: session 0x%x ('%s') no handler", session_handle, session->service_name.c_str());
        return 0;
    }

    // ── IPC 追踪：请求 ────────────────────────────────────
    TRACE_IPC(cmd_id, (u64)session_handle, raw_in_size > 0 ? *((const u64*)raw_in) : 0, 0);

    // HID 诊断: 打印所有 HID 请求的完整 64 字节 hex
    if (sname.find("hid") != std::string::npos) {
        char hex[128] = {};
        int n = 0;
        for (size_t i = 0; i < std::min(size, (size_t)32); i++)
            n += snprintf(hex + n, sizeof(hex) - n, "%02x", data[i]);
        LOG_INFO("HID_DIAG: session=0x%x cmd=%u type=%u hex=%s sz=%zu",
                  session_handle, cmd_id, (unsigned)req_hdr.type, hex, size);
    }

    // ── 处理 Control 命令（type=5）────────────────────
    // Control 命令由 IPC 层直接处理，不转发给服务
    bool handled = false;
    // is_control 已在 handle 预分配前声明
    
    if (is_control) {
        if (cmd_id == 0) {
            // ConvertCurrentObjectToDomain — 返回 domain_id 让 libnx 进入 domain 模式
            // appletInitialize 依赖此成功，否则整个初始化失败
            LOG_DEBUG("IPC: ConvertCurrentObjectToDomain → domain_id=1 for session 0x%x", session_handle);
            if (raw_out_max >= 4) {
                u32 domain_id = 1;
                std::memcpy(raw_out, &domain_id, sizeof(domain_id));
                raw_out_max = 4;
            }
            session->is_domain = true; // 记住 domain 状态供后续请求使用
            LOG_DEBUG("IPC: session 0x%x now in domain mode", session_handle);
            handled = true;
        } else if (cmd_id == 3) {
            // QueryPointerBufferSize — 返回 u16=0（无指针缓冲需求）
            if (raw_out_max >= 2) {
                u16 buf_size = 0;
                std::memcpy(raw_out, &buf_size, sizeof(buf_size));
                raw_out_max = 2;
            } else {
                raw_out_max = 0;
            }
            handled = true;
        } else {
            LOG_TRACE("IPC: unhandled control cmd %u for session 0x%x", cmd_id, session_handle);
            handled = true;
            raw_out_max = 0;
        }
    } else {
        // ── 普通 Request 命令 → 转发给服务 ─────────────────
        if (session->service) {
            handled = session->service->HandleCommand(cmd_id, raw_in, raw_in_size, raw_out, &raw_out_max);
        } else {
            LOG_WARN("IPC: no service handler for session 0x%x ('%s')", session_handle, session->service_name.c_str());
            raw_out_max = 0;
            handled = true;
        }
    }

    LOG_DEBUG("IPC: handled=%d is_control=%d cmd=%u for session 0x%x service=%s", 
              handled, is_control, cmd_id, session_handle, svc_name_cstr);

    // 后处理：从 raw_out 提取 move/copy handle（服务将句柄写入 raw_out[0..3]）
    // 控制命令(type=5)不提取，防止 domain_id 被误提取为句柄
    if (handled && handle_pos && !is_control && raw_out_max >= 4) {
        u32 move_handle = 0;
        std::memcpy(&move_handle, raw_out, sizeof(move_handle));
        if (move_handle != 0) { // 验证是有效句柄 (session 0x1000+ 或 kernel 0xD000+)
            LOG_DEBUG("IPC: move handle 0x%x for '%s' cmd=%u",
                      move_handle, svc_name_cstr, cmd_id);
            std::memcpy(handle_pos, &move_handle, sizeof(move_handle));
            raw_out_max = 0; // handle 已提取到头部，清除数据
        }
    }

    if (handled) {
        // ── 写入 recv_list 输出缓冲 ──────────────────
        // 如果请求中包含 recv_list（输出缓冲描述符），将服务响应数据写入其中
        if (recv_list.count > 0 && raw_out_max > 0 && g_mem) {
            // 只写入第一个 recv_list 条目（绝大多数情况只有一个输出缓冲）
            u64 guest_addr = recv_list.entries[0].guest_addr;
            u32 buf_size = recv_list.entries[0].size;
            u32 write_size = std::min<u32>((u32)raw_out_max, buf_size);
            
            // 将 guest_addr 从 CPU 绝对地址转换为 guest 相对地址
            u64 mem_base = g_mem->BaseAddress();
            u64 rel_addr = guest_addr;
            if (guest_addr >= mem_base && guest_addr < mem_base + Memory::ADDR_SPACE_SIZE) {
                rel_addr = guest_addr - mem_base;
            }
            
            // 逐字节写入 guest 内存
            for (u32 i = 0; i < write_size; i++) {
                g_mem->Write(rel_addr + i, raw_out[i]);
            }
            LOG_DEBUG("IPC: wrote %u bytes to recv_list buffer guest=0x%llx",
                      write_size, guest_addr);
            // 数据已写入缓冲 → 清除 inline 输出，响应只需 CMIF 头
            raw_out_max = 0;
        }

        // ── 构建 CMIF 响应 ──────────────────────────────
        // Control 命令(type=5): CmifOutHeader 在 data_words 开始处（无对齐）
        // Request 命令(type=4): CmifOutHeader 在 16 字节对齐处
        size_t raw_off = (size_t)(raw_out - response);
        size_t cmif_out_off = is_control ? raw_off : CalcCmifAlign(raw_off);
        
        // cmif_out_off 处放 CmifOutHeader (16 bytes)
        // cmif_out_off + 16 处放 service 数据
        size_t cmif_out_end = cmif_out_off + sizeof(CmifOutHeader);
        size_t total_data_end = cmif_out_end + raw_out_max;
        
        // 将 service 输出数据后移到 CmifOutHeader 之后
        if (raw_out_max > 0 && cmif_out_end > raw_off) {
            // 对于 Control 命令，raw_off==cmif_out_off，需要整体后移
            memmove(response + cmif_out_end, response + raw_off, raw_out_max);
        }
        // 对于 Control 命令：raw_out 已经在 cmif_out_off 处，如果raw_off == cmif_out_off
        // 且 raw_out_max==0，无需移动
        
        // 写入 CmifOutHeader（回填请求中的 token）
        CmifOutHeader cmif_out = {};
        cmif_out.magic  = CMIF_OUT_MAGIC;
        cmif_out.token  = cmif_token;
        cmif_out.result = 0;
        std::memcpy(response + cmif_out_off, &cmif_out, sizeof(cmif_out));
        
        // 计算总 data_words 大小（从数据偏移到结尾，对齐到 4 字节）
        size_t total_data_size = total_data_end - raw_off;
        resp_hdr.num_data_words = (u32)((total_data_size + 3) / 4);
        
        size_t header_size = raw_off; // HipcHeader + 特殊头 + 句柄
        size_t total_size = header_size + total_data_size;
        
        // 写入 SpecialHeader
        if (shdr_pos) {
            HipcSpecialHeader shdr = {};
            shdr.num_copy_handles = num_copy_handles;
            shdr.num_move_handles = num_move_handles;
            std::memcpy(shdr_pos, &shdr, sizeof(shdr));
        }
        
        *resp_size = total_size;
    } else {
        LOG_WARN("IPC: unhandled cmd %u for '%s'", cmd_id, session->service_name.c_str());
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

    // HID 诊断：输出完整 64 字节响应
    if ((sname == "hid" || sname == "hid:") && cmd_id == 0) {
        char hex[256] = {};
        int n = 0;
        size_t dump_sz = *resp_size < 64 ? *resp_size : 64;
        for (size_t i = 0; i < dump_sz; i++)
            n += snprintf(hex + n, sizeof(hex) - n, "%02x", response[i]);
        LOG_INFO("HID_RESP: sz=%zu raw_off=%zu num_dw=%u special=%u copy=%u: %s",
                  *resp_size, (size_t)(raw_out - response), resp_hdr.num_data_words,
                  resp_hdr.has_special_header, num_copy_handles, hex);
    }

    TRACE_IPC(cmd_id | 0x8000, (u64)session_handle, (u64)handled, 0);
    return 0;
}

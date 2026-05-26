#include "debug/DebugServer.h"
#include "debug/SnapshotManager.h"
#include "cpu/Debugger.h"
#include "kernel/Kernel.h"
#include "memory/Memory.h"
#include "common/Log.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <algorithm>

// ── 简易 JSON 解析 ─────────────────────────────────────────────
// 只处理 {"cmd":"xxx","args":["a","b"]} 格式

static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    // 找到冒号后的值
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return "";

    // 跳过空白
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // 字符串值
        size_t start = pos + 1;
        size_t end = json.find('"', start);
        if (end == std::string::npos) return "";
        return json.substr(start, end - start);
    }
    return "";
}

static std::vector<std::string> ExtractJsonArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return result;

    pos = json.find('[', pos + search.size());
    if (pos == std::string::npos) return result;

    size_t end = json.find(']', pos);
    if (end == std::string::npos) return result;

    std::string arr = json.substr(pos + 1, end - pos - 1);
    // 分割逗号分隔的字符串
    size_t i = 0;
    while (i < arr.size()) {
        // 找下一个引号
        while (i < arr.size() && arr[i] != '"') i++;
        if (i >= arr.size()) break;
        i++; // 跳过开头引号
        size_t start = i;
        while (i < arr.size() && arr[i] != '"') i++;
        result.push_back(arr.substr(start, i - start));
        i++; // 跳过结尾引号
    }
    return result;
}

static u64 ParseHex(const std::string& s) {
    return std::stoull(s, nullptr, 0);
}

// ── DebugCommand 解析 ─────────────────────────────────────────

DebugCommand DebugCommand::Parse(const std::string& json) {
    DebugCommand cmd;
    cmd.cmd = ExtractJsonString(json, "cmd");
    cmd.args = ExtractJsonArray(json, "args");
    return cmd;
}

std::string DebugCommand::ToJson() const {
    std::string result = "{\"cmd\":\"" + cmd + "\",\"args\":[";
    for (size_t i = 0; i < args.size(); i++) {
        if (i) result += ",";
        result += "\"" + args[i] + "\"";
    }
    result += "]}";
    return result;
}

// ── DebugResponse 序列化 ──────────────────────────────────────

std::string DebugResponse::ToJson() const {
    std::string result = "{\"success\":" + std::string(success ? "true" : "false");
    if (!error.empty()) {
        result += ",\"error\":\"" + error + "\"";
    }
    if (!data.empty()) {
        result += ",\"data\":" + data;
    }
    result += "}";
    return result;
}

// ── 单例 ───────────────────────────────────────────────────────

DebugServer& DebugServer::Instance() {
    static DebugServer instance;
    return instance;
}

DebugServer::DebugServer() {
    // 注册内置命令
    commands_["break"] = [this](const auto& a) { return CmdBreak(a); };
    commands_["break_remove"] = [this](const auto& a) { return CmdBreakRemove(a); };
    commands_["breakpoints"] = [this](const auto&) { return CmdBreakpoints(); };
    commands_["continue"] = [this](const auto&) { return CmdContinue(); };
    commands_["step"] = [this](const auto&) { return CmdStep(); };
    commands_["pause"] = [this](const auto&) { return CmdPause(); };
    commands_["regs"] = [this](const auto&) { return CmdRegs(); };
    commands_["read"] = [this](const auto& a) { return CmdRead(a); };
    commands_["write"] = [this](const auto& a) { return CmdWrite(a); };
    commands_["trace"] = [this](const auto& a) { return CmdTrace(a); };
    commands_["svc_trace"] = [this](const auto& a) { return CmdSvcTrace(a); };
    commands_["ipc_trace"] = [this](const auto& a) { return CmdIpcTrace(a); };
    commands_["gpu_trace"] = [this](const auto& a) { return CmdGpuTrace(a); };
    commands_["snap"] = [this](const auto&) { return CmdSnap(); };
    commands_["stats"] = [this](const auto&) { return CmdStats(); };
    commands_["threads"] = [this](const auto&) { return CmdThreads(); };
    commands_["help"] = [this](const auto&) { return CmdHelp(); };
}

DebugServer::~DebugServer() {
    Stop();
}

// ── 服务器启动/停止 ────────────────────────────────────────────

void DebugServer::Start(const std::string& socket_path) {
    if (running_.load()) return;

    socket_path_ = socket_path;

    // 移除旧 socket 文件
    unlink(socket_path_.c_str());

    socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        LOG_ERROR("DebugServer: socket() 失败: %s", strerror(errno));
        return;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("DebugServer: bind() 失败: %s", strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        return;
    }

    if (listen(socket_fd_, 4) < 0) {
        LOG_ERROR("DebugServer: listen() 失败: %s", strerror(errno));
        close(socket_fd_);
        socket_fd_ = -1;
        unlink(socket_path_.c_str());
        return;
    }

    running_.store(true);
    server_thread_ = std::thread(&DebugServer::ServerLoop, this);
    LOG_INFO("DebugServer: 监听 %s", socket_path_.c_str());
}

void DebugServer::Stop() {
    if (!running_.load()) return;
    running_.store(false);

    if (socket_fd_ >= 0) {
        shutdown(socket_fd_, SHUT_RDWR);
        close(socket_fd_);
        socket_fd_ = -1;
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    unlink(socket_path_.c_str());
    LOG_INFO("DebugServer: 已停止");
}

// ── 服务器主循环 ───────────────────────────────────────────────

void DebugServer::ServerLoop() {
    pthread_setname_np("DebugServer");

    while (running_.load()) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(socket_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_.load()) {
                LOG_ERROR("DebugServer: accept() 失败: %s", strerror(errno));
            }
            continue;
        }

        LOG_INFO("DebugServer: 客户端连接");

        // 处理客户端 (单线程模式，简单可靠)
        HandleClient(client_fd);
        close(client_fd);

        LOG_INFO("DebugServer: 客户端断开");
    }
}

void DebugServer::HandleClient(int client_fd) {
    char buf[4096];
    std::string line_buf;

    while (running_.load()) {
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;

        buf[n] = '\0';
        line_buf += buf;

        // 按行处理 (每行一个 JSON 命令)
        size_t pos;
        while ((pos = line_buf.find('\n')) != std::string::npos) {
            std::string line = line_buf.substr(0, pos);
            line_buf.erase(0, pos + 1);

            // 去除空白
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty()) continue;

            DebugCommand cmd = DebugCommand::Parse(line);
            DebugResponse resp = HandleCommand(cmd);

            std::string resp_str = resp.ToJson() + "\n";
            write(client_fd, resp_str.c_str(), resp_str.size());
        }
    }
}

// ── 命令分发 ───────────────────────────────────────────────────

DebugResponse DebugServer::HandleCommand(const DebugCommand& cmd) {
    std::lock_guard<std::mutex> lock(cmd_mutex_);

    auto it = commands_.find(cmd.cmd);
    if (it != commands_.end()) {
        return it->second(cmd.args);
    }

    DebugResponse resp;
    resp.success = false;
    resp.error = "未知命令: " + cmd.cmd + " (输入 help 查看命令列表)";
    return resp;
}

// ── 断点命令 ───────────────────────────────────────────────────

DebugResponse DebugServer::CmdBreak(const std::vector<std::string>& args) {
    DebugResponse resp;
    if (args.empty()) {
        resp.success = false;
        resp.error = "用法: break <地址>";
        return resp;
    }

    u64 addr = ParseHex(args[0]);
    auto& dbg = GlobalDebugger();
    dbg.SetBreakpoint(addr);

    LOG_INFO("DebugServer: 设置断点 0x%llx", addr);
    resp.data = "{\"addr\":\"0x" + std::string(args[0]) + "\"}";
    return resp;
}

DebugResponse DebugServer::CmdBreakRemove(const std::vector<std::string>& args) {
    DebugResponse resp;
    if (args.empty()) {
        resp.success = false;
        resp.error = "用法: break_remove <地址>";
        return resp;
    }

    u64 addr = ParseHex(args[0]);
    auto& dbg = GlobalDebugger();
    dbg.RemoveBreakpoint(addr);

    LOG_INFO("DebugServer: 移除断点 0x%llx", addr);
    resp.data = "{\"removed\":\"0x" + std::string(args[0]) + "\"}";
    return resp;
}

DebugResponse DebugServer::CmdBreakpoints() {
    DebugResponse resp;
    auto& dbg = GlobalDebugger();
    auto bps = dbg.GetBreakpoints();

    std::string data = "[";
    for (size_t i = 0; i < bps.size(); i++) {
        if (i) data += ",";
        char addr_str[32];
        snprintf(addr_str, sizeof(addr_str), "0x%llx", bps[i].guest_address);
        data += "{\"addr\":\"" + std::string(addr_str) +
                "\",\"hits\":" + std::to_string(bps[i].hit_count) +
                ",\"enabled\":" + (bps[i].enabled ? "true" : "false") + "}";
    }
    data += "]";
    resp.data = data;
    return resp;
}

DebugResponse DebugServer::CmdContinue() {
    auto& dbg = GlobalDebugger();
    dbg.Continue();
    DebugResponse resp;
    resp.data = "{\"status\":\"running\"}";
    return resp;
}

DebugResponse DebugServer::CmdStep() {
    auto& dbg = GlobalDebugger();
    dbg.StepOver();
    DebugResponse resp;
    resp.data = "{\"status\":\"stepping\"}";
    return resp;
}

DebugResponse DebugServer::CmdPause() {
    auto& dbg = GlobalDebugger();
    dbg.Pause();
    DebugResponse resp;
    resp.data = "{\"status\":\"paused\"}";
    return resp;
}

// ── 寄存器 ─────────────────────────────────────────────────────

DebugResponse DebugServer::CmdRegs() {
    DebugResponse resp;
    auto& dbg = GlobalDebugger();
    auto regs = dbg.GetLastRegisters();

    std::ostringstream data;
    data << "{";
    for (int i = 0; i < 31; i++) {
        if (i) data << ",";
        data << "\"x" << i << "\":\"0x" << std::hex << regs.x[i] << "\"";
    }
    data << ",\"sp\":\"0x" << std::hex << regs.sp << "\"";
    data << ",\"pc\":\"0x" << std::hex << regs.pc << "\"";
    data << ",\"pstate\":\"0x" << std::hex << regs.pstate << "\"";
    data << "}";

    resp.data = data.str();
    return resp;
}

// ── 内存读写 ───────────────────────────────────────────────────

DebugResponse DebugServer::CmdRead(const std::vector<std::string>& args) {
    DebugResponse resp;
    if (args.size() < 2) {
        resp.success = false;
        resp.error = "用法: read <地址> <大小>";
        return resp;
    }

    u64 addr = ParseHex(args[0]);
    size_t size = (size_t)ParseHex(args[1]);
    if (size == 0 || size > 4096) size = 256;

    auto& dbg = GlobalDebugger();
    auto mem = dbg.ReadMemory(addr, size);

    // 转为 hex 字符串
    std::ostringstream data;
    data << "{\"addr\":\"0x" << std::hex << addr << "\",\"size\":" << std::dec << size << ",\"hex\":\"";
    for (u8 b : mem) {
        data << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    }
    data << "\"}";
    resp.data = data.str();
    return resp;
}

DebugResponse DebugServer::CmdWrite(const std::vector<std::string>& args) {
    DebugResponse resp;
    if (args.size() < 2) {
        resp.success = false;
        resp.error = "用法: write <地址> <hex数据>";
        return resp;
    }

    u64 addr = ParseHex(args[0]);
    std::vector<u8> data;
    const std::string& hex = args[1];
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        u8 byte = (u8)std::stoi(hex.substr(i, 2), nullptr, 16);
        data.push_back(byte);
    }

    auto& dbg = GlobalDebugger();
    bool ok = dbg.WriteMemory(addr, data);
    resp.success = ok;
    if (!ok) {
        resp.error = "写入内存失败";
    } else {
        resp.data = "{\"written\":" + std::to_string(data.size()) + "}";
    }
    return resp;
}

// ── 追踪通道控制 ──────────────────────────────────────────────

DebugResponse DebugServer::CmdTrace(const std::vector<std::string>& args) {
    DebugResponse resp;
    if (args.size() < 2) {
        resp.success = false;
        resp.error = "用法: trace <通道名> <on|off>  通道: svc,ipc,gpu,mem,cpu,thread,all";
        return resp;
    }

    auto& trace = TraceEngine::Instance();
    std::string ch_name = args[0];
    std::string state = args[1];
    bool enable = (state == "on" || state == "1" || state == "true");

    TraceChannel ch;
    if (ch_name == "svc") ch = TraceChannel::SVC;
    else if (ch_name == "ipc") ch = TraceChannel::IPC;
    else if (ch_name == "gpu") ch = TraceChannel::GPU_CMD;
    else if (ch_name == "mem") ch = TraceChannel::MEM;
    else if (ch_name == "cpu") ch = TraceChannel::CPU_EXEC;
    else if (ch_name == "thread") ch = TraceChannel::THREAD;
    else if (ch_name == "all") {
        if (enable) trace.EnableAll(); else trace.DisableAll();
        resp.data = "{\"action\":\"" + std::string(enable ? "enabled" : "disabled") + "\",\"channel\":\"all\"}";
        return resp;
    } else {
        resp.success = false;
        resp.error = "未知通道: " + ch_name;
        return resp;
    }

    trace.EnableChannel(ch, enable);
    resp.data = "{\"action\":\"" + std::string(enable ? "enabled" : "disabled") +
                "\",\"channel\":\"" + ch_name + "\"}";
    return resp;
}

// ── 追踪查询 ───────────────────────────────────────────────────

static std::string FormatTraceEvents(const std::vector<TraceEvent>& events) {
    std::ostringstream data;
    data << "[";
    for (size_t i = 0; i < events.size(); i++) {
        if (i) data << ",";
        const auto& e = events[i];
        data << "{\"ts\":" << std::dec << e.timestamp;
        data << ",\"ch\":\"" << TraceChannelNames[(u32)e.channel] << "\"";
        data << ",\"eid\":" << e.event_id;
        if (e.guest_pc) data << ",\"pc\":\"0x" << std::hex << e.guest_pc << "\"";
        data << ",\"tid\":" << std::dec << e.thread_id;
        data << ",\"a0\":\"0x" << std::hex << e.args[0] << "\"";
        data << ",\"a1\":\"0x" << e.args[1] << "\"";
        data << ",\"res\":\"0x" << e.result << "\"";
        data << "}";
    }
    data << "]";
    return data.str();
}

DebugResponse DebugServer::CmdSvcTrace(const std::vector<std::string>& args) {
    DebugResponse resp;
    u32 count = args.empty() ? 50 : (u32)ParseHex(args[0]);
    auto events = TraceEngine::Instance().Query(TraceChannel::SVC, 0, UINT64_MAX, count);
    resp.data = FormatTraceEvents(events);
    return resp;
}

DebugResponse DebugServer::CmdIpcTrace(const std::vector<std::string>& args) {
    DebugResponse resp;
    u32 count = args.empty() ? 50 : (u32)ParseHex(args[0]);
    auto events = TraceEngine::Instance().Query(TraceChannel::IPC, 0, UINT64_MAX, count);
    resp.data = FormatTraceEvents(events);
    return resp;
}

DebugResponse DebugServer::CmdGpuTrace(const std::vector<std::string>& args) {
    DebugResponse resp;
    u32 count = args.empty() ? 50 : (u32)ParseHex(args[0]);
    auto events = TraceEngine::Instance().Query(TraceChannel::GPU_CMD, 0, UINT64_MAX, count);
    resp.data = FormatTraceEvents(events);
    return resp;
}

// ── 快照 ────────────────────────────────────────────────────────

DebugResponse DebugServer::CmdSnap() {
    DebugResponse resp;
    auto& snap = SnapshotManager::Instance();
    auto s = snap.Capture();

    std::ostringstream data;
    data << "{\"pc\":\"0x" << std::hex << s.regs.pc << "\"";
    data << ",\"sp\":\"0x" << s.regs.sp << "\"";
    data << ",\"x0\":\"0x" << s.regs.x[0] << "\"";
    data << ",\"x1\":\"0x" << s.regs.x[1] << "\"";
    data << ",\"gpu_draws\":" << std::dec << s.gpu.draw_count;
    data << ",\"recent_svcs\":[";
    for (size_t i = 0; i < s.recent_svcs.size(); i++) {
        if (i) data << ",";
        data << s.recent_svcs[i];
    }
    data << "]}";

    resp.data = data.str();
    return resp;
}

// ── 统计 ───────────────────────────────────────────────────────

DebugResponse DebugServer::CmdStats() {
    DebugResponse resp;
    auto stats = TraceEngine::Instance().GetStats();

    std::ostringstream data;
    data << "{\"total\":" << stats.total_events;
    data << ",\"dropped\":" << stats.dropped_events;
    data << ",\"channels\":{";
    for (size_t i = 0; i < (size_t)TraceChannel::COUNT; i++) {
        if (i) data << ",";
        data << "\"" << TraceChannelNames[i] << "\":" << stats.events_per_channel[i];
    }
    data << "}}";

    resp.data = data.str();
    return resp;
}

// ── 线程列表 ────────────────────────────────────────────────────

DebugResponse DebugServer::CmdThreads() {
    DebugResponse resp;
    // TODO: 从 Scheduler 获取线程列表
    resp.data = "{\"threads\":[],\"note\":\"需要集成 Scheduler\"}";
    return resp;
}

// ── 帮助 ───────────────────────────────────────────────────────

DebugResponse DebugServer::CmdHelp() {
    DebugResponse resp;
    resp.data = "{\"commands\":[" 
        "{\"cmd\":\"break <addr>\",\"desc\":\"设置断点\"},"
        "{\"cmd\":\"break_remove <addr>\",\"desc\":\"移除断点\"},"
        "{\"cmd\":\"breakpoints\",\"desc\":\"列出断点\"},"
        "{\"cmd\":\"continue\",\"desc\":\"继续执行\"},"
        "{\"cmd\":\"step\",\"desc\":\"单步\"},"
        "{\"cmd\":\"pause\",\"desc\":\"暂停\"},"
        "{\"cmd\":\"regs\",\"desc\":\"读寄存器\"},"
        "{\"cmd\":\"read <addr> <size>\",\"desc\":\"读内存\"},"
        "{\"cmd\":\"write <addr> <hex>\",\"desc\":\"写内存\"},"
        "{\"cmd\":\"trace <ch> <on|off>\",\"desc\":\"开关trace (svc/ipc/gpu/mem/cpu/thread/all)\"},"
        "{\"cmd\":\"svc_trace [n]\",\"desc\":\"最近n条SVC\"},"
        "{\"cmd\":\"ipc_trace [n]\",\"desc\":\"最近n条IPC\"},"
        "{\"cmd\":\"gpu_trace [n]\",\"desc\":\"最近n条GPU\"},"
        "{\"cmd\":\"snap\",\"desc\":\"取快照\"},"
        "{\"cmd\":\"stats\",\"desc\":\"trace统计\"},"
        "{\"cmd\":\"threads\",\"desc\":\"列出线程\"}"
        "]}";
    return resp;
}

// ── 断点回调 ──────────────────────────────────────────────────

void DebugServer::SetBreakpointCallback(BreakpointCallback cb) {
    bp_callback_ = std::move(cb);
}
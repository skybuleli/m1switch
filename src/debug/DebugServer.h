#pragma once

#include "common/Types.h"
#include "debug/TraceEngine.h"

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

// ── 调试服务器 ────────────────────────────────────────────────
// 通过 Unix domain socket 提供远程调试接口。
//
// 协议：JSON-RPC 风格，每个请求一行 JSON，每个响应一行 JSON。
//
// 命令列表：
//   {"cmd":"break","args":["0x71000ABC"]}        设置断点
//   {"cmd":"continue"}                            继续执行
//   {"cmd":"step"}                                单步
//   {"cmd":"pause"}                               暂停
//   {"cmd":"regs"}                                读寄存器
//   {"cmd":"read","args":["0x80000000","256"]}    读内存
//   {"cmd":"write","args":["0x80000000","deadbeef"]}  写内存
//   {"cmd":"trace","args":["svc","on"]}           开关trace通道
//   {"cmd":"svc_trace","args":["100"]}            最近N条SVC
//   {"cmd":"ipc_trace","args":["100"]}            最近N条IPC
//   {"cmd":"gpu_trace","args":["100"]}             最近N条GPU
//   {"cmd":"snap"}                                取快照
//   {"cmd":"stats"}                               trace统计
//   {"cmd":"threads"}                             列出线程
//   {"cmd":"breakpoints"}                         列出断点

struct DebugCommand {
    std::string cmd;
    std::vector<std::string> args;

    // 从 JSON 字符串解析
    static DebugCommand Parse(const std::string& json);

    // 序列化为 JSON
    std::string ToJson() const;
};

struct DebugResponse {
    bool success = true;
    std::string error;
    std::string data;    // JSON 格式的结果数据

    std::string ToJson() const;
};

class DebugServer {
public:
    static DebugServer& Instance();

    // 启动/停止服务器
    void Start(const std::string& socket_path = "/tmp/m1switch_debug.sock");
    void Stop();

    // 检查服务器是否运行
    bool IsRunning() const { return running_.load(); }

    // 设置断点回调 (由 EmuDebugger 注册)
    using BreakpointCallback = std::function<void(u64 addr)>;
    void SetBreakpointCallback(BreakpointCallback cb);

private:
    DebugServer();
    ~DebugServer();
    DebugServer(const DebugServer&) = delete;
    DebugServer& operator=(const DebugServer&) = delete;

    // 服务器线程主循环
    void ServerLoop();
    // 处理单个客户端连接
    void HandleClient(int client_fd);
    // 处理单个命令
    DebugResponse HandleCommand(const DebugCommand& cmd);

    // ── 内置命令处理 ────────────────────────────────
    DebugResponse CmdBreak(const std::vector<std::string>& args);
    DebugResponse CmdBreakRemove(const std::vector<std::string>& args);
    DebugResponse CmdBreakpoints();
    DebugResponse CmdContinue();
    DebugResponse CmdStep();
    DebugResponse CmdPause();
    DebugResponse CmdRegs();
    DebugResponse CmdRead(const std::vector<std::string>& args);
    DebugResponse CmdWrite(const std::vector<std::string>& args);
    DebugResponse CmdTrace(const std::vector<std::string>& args);
    DebugResponse CmdSvcTrace(const std::vector<std::string>& args);
    DebugResponse CmdIpcTrace(const std::vector<std::string>& args);
    DebugResponse CmdGpuTrace(const std::vector<std::string>& args);
    DebugResponse CmdSnap();
    DebugResponse CmdStats();
    DebugResponse CmdThreads();
    DebugResponse CmdHelp();

    int socket_fd_ = -1;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    std::string socket_path_;
    BreakpointCallback bp_callback_;
    mutable std::mutex cmd_mutex_;

    // 命令表
    using CmdHandler = std::function<DebugResponse(const std::vector<std::string>&)>;
    std::unordered_map<std::string, CmdHandler> commands_;
};
#pragma once

#include "common/Types.h"
#include <signal.h>
#include <functional>

struct __attribute__((aligned(16))) GuestThreadState {
    u64 x[31];
    u64 sp;
    u64 pc;
};

// BRK 标签常量（也在 Debugger 和 NativeExec 中使用）
constexpr u32 BRK_TAG_DEBUG = 0x6000;
constexpr u32 BRK_TAG_BASE  = 0x1000;

using SvcHandlerFn = std::function<void(u32 svc_num, GuestThreadState* state)>;

class SigHandler {
public:
    SigHandler();
    ~SigHandler();
    SigHandler(const SigHandler&) = delete;
    SigHandler& operator=(const SigHandler&) = delete;

    void SetSvcDispatch(SvcHandlerFn fn);
    Result Install();

    static void EnsureInstalled();

private:
    static void TrapHandler(int sig, siginfo_t* info, void* uap);
    static std::atomic<bool> s_installed;
    static struct sigaction s_old_action;

    SvcHandlerFn dispatch_;
};

// JIT 区域跟踪（由 Memory/加载器写入，信号处理函数读取用于 MAP_JIT W/X 切换）
extern uint64_t g_jit_region_start;
extern uint64_t g_jit_region_end;

void SigHandler_EnsureInstalled();

// 设置当前线程的替代信号栈（避免信号处理函数覆盖 guest 栈数据）
// 任何执行 guest 代码的线程必须在运行 guest 前调用此函数
Result SetupGuestSignalStack();

// BRK 地址缓存（信号处理函数零内存读取查询 SVC tag）
void BrkCache_Add(u64 host_pc, u32 svc_num);
void BrkCache_Clear();


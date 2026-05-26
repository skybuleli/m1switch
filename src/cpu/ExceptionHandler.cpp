#include "cpu/ExceptionHandler.h"
#include "cpu/Debugger.h"
#include "kernel/SvcTable.h"
#include "common/Log.h"
#include <mach/mach.h>
#include <sys/ucontext.h>
#include <csetjmp>
#include <array>

extern thread_local sigjmp_buf g_guest_exit_jmp_buf;
extern thread_local bool g_guest_exit_jmp_valid;
extern std::atomic<bool> g_guest_crashed;

constexpr u32 BRK_MASK = 0xFFE0001F;  // bits [31:24]=0xD4, [23:21]=000, [4:0]=00000
constexpr u32 BRK_PATTERN = 0xD4200000;
constexpr u32 BRK_TAG_BASE = 0x1000;

std::atomic<bool> SigHandler::s_installed{false};
struct sigaction SigHandler::s_old_action{};

SigHandler::SigHandler() {}
SigHandler::~SigHandler() = default;

void SigHandler::SetSvcDispatch(SvcHandlerFn fn) { dispatch_ = std::move(fn); }

void SigHandler::TrapHandler(int sig, siginfo_t* info, void* uap) {
    (void)info;
    auto* uc = static_cast<ucontext_t*>(uap);
    auto& ss = uc->uc_mcontext->__ss;
    u64 pc = ss.__pc;

    if (sig != SIGTRAP) {
        LOG_ERROR("Guest signal %d at PC=0x%llx SP=0x%llx LR=0x%llx fault=%p",
                  sig, pc, (u64)ss.__sp, (u64)ss.__lr, info ? info->si_addr : nullptr);
        LOG_ERROR("  x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx",
                  (u64)ss.__x[0], (u64)ss.__x[1], (u64)ss.__x[2], (u64)ss.__x[3]);
        g_guest_crashed.store(true);
        g_guest_exited.store(true);
        if (g_guest_exit_jmp_valid) {
            siglongjmp(g_guest_exit_jmp_buf, 1);
        }
        pthread_exit(nullptr);
    }

    u32 inst;
    vm_size_t rsz;
    kern_return_t kr = vm_read_overwrite(mach_task_self(), pc, sizeof(inst),
                      reinterpret_cast<vm_address_t>(&inst), &rsz);

    // 如果 vm_read 失败（Apple Silicon 上对 JIT/RX pages 可能发生），
    // 使用 memcpy 直接读取
    if (kr != KERN_SUCCESS) {
        // 尝试直接 memcpy（页面可能 RX 但 vm_read 受限于代码签名策略）
        LOG_WARN("vm_read_overwrite failed at PC=0x%llx kr=%d, trying memcpy", pc, kr);
        inst = *reinterpret_cast<const u32*>(pc);
        LOG_INFO("memcpy read: inst=0x%08x at PC=0x%llx", inst, pc);
    }

    bool is_brk = ((inst & BRK_MASK) == BRK_PATTERN);
    LOG_INFO("TrapHandler: PC=0x%llx inst=0x%08x is_brk=%d", pc, inst, is_brk);

    if (is_brk) {
        u32 tag = (inst >> 5) & 0xFFFF;

        // 调试器断点 (BRK 0x6000+)
        if ((tag & 0xF000) == BRK_TAG_DEBUG) {
            GuestThreadState gs;
            for (int i = 0; i < 29; i++) gs.x[i] = ss.__x[i];
            gs.x[29] = ss.__fp;
            gs.x[30] = ss.__lr;
            gs.sp = ss.__sp;
            gs.pc = pc;

            bool handled = GlobalDebugger().OnBreakpoint(pc, gs);

            if (handled) {
                for (int i = 0; i < 29; i++) ss.__x[i] = gs.x[i];
                ss.__fp = gs.x[29];
                ss.__lr = gs.x[30];
                ss.__sp = gs.sp;
                // 不推进 PC — 调试器会在继续时恢复原始指令并单步
                return;
            }
            ss.__pc = pc + 4;
            return;
        }

        u32 svc = tag - BRK_TAG_BASE;

        LOG_INFO("SVC #0x%02x caught at PC=0x%llx (tag=0x%x)", svc, pc, tag);

        GuestThreadState gs;
        for (int i = 0; i < 29; i++) gs.x[i] = ss.__x[i];
        gs.x[29] = ss.__fp;
        gs.x[30] = ss.__lr;
        gs.sp = ss.__sp;
        gs.pc = pc;

        SvcHandler_Dispatch(svc, &gs);

        for (int i = 0; i < 29; i++) ss.__x[i] = gs.x[i];
        ss.__fp = gs.x[29];
        ss.__lr = gs.x[30];
        ss.__sp = gs.sp;

        // 如果 guest 已退出（SvcExitProcess），用 siglongjmp 安全地跳出信号处理
        if (g_guest_exited.load()) {
            if (g_guest_exit_jmp_valid) {
                siglongjmp(g_guest_exit_jmp_buf, 1);
            }
            // 无有效的 jmp_buf 时，让线程退出
            pthread_exit(nullptr);
        }
    }
    ss.__pc = pc + 4;
}

Result SigHandler::Install() {
    EnsureInstalled();
    return Result::Success;
}

// 专用信号栈（避免信号处理函数覆盖 guest 栈上的数据）
static std::array<char, 32768> s_signal_stack;  // 32KB 信号栈

void SigHandler::EnsureInstalled() {
    bool expected = false;
    if (!s_installed.compare_exchange_strong(expected, true)) return;

    // 为信号处理函数设置专用栈
    stack_t ss;
    ss.ss_sp = s_signal_stack.data();
    ss.ss_size = s_signal_stack.size();
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa = {};
    sa.sa_sigaction = TrapHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTRAP, &sa, &s_old_action) != 0 ||
        sigaction(SIGSEGV, &sa, nullptr) != 0 ||
        sigaction(SIGBUS, &sa, nullptr) != 0 ||
        sigaction(SIGILL, &sa, nullptr) != 0) {
        LOG_ERROR("sigaction guest handlers failed");
        s_installed.store(false);
        return;
    }
    LOG_DEBUG("Guest signal handlers installed (global, install-once): alt_stack=%p size=%zu",
              s_signal_stack.data(), s_signal_stack.size());
}

void SigHandler_EnsureInstalled() {
    SigHandler::EnsureInstalled();
}

#include "cpu/ExceptionHandler.h"
#include "cpu/Debugger.h"
#include "kernel/SvcTable.h"
#include "common/Log.h"
#include <mach/mach.h>
#include <sys/ucontext.h>
#include <csetjmp>
#include <array>
#include <unordered_map>

// BRK 地址 → SVC tag 缓存（信号处理函数零内存读取查询）
// 由 PatchSVCs 的调用者在打补丁时填充
std::unordered_map<u64, u32> g_brk_cache;

// 由调用者填充 BRK 缓存
void BrkCache_Add(u64 host_addr, u32 svc_num) {
    u32 tag = BRK_TAG_BASE + svc_num;
    g_brk_cache[host_addr] = tag;
}

void BrkCache_Clear() { g_brk_cache.clear(); }

extern thread_local sigjmp_buf g_guest_exit_jmp_buf;
extern thread_local bool g_guest_exit_jmp_valid;
extern std::atomic<bool> g_guest_crashed;

constexpr u32 BRK_MASK = 0xFFE0001F;  // bits [31:24]=0xD4, [23:21]=000, [4:0]=00000
constexpr u32 BRK_PATTERN = 0xD4200000;
// BRK_TAG_BASE 定义在 ExceptionHandler.h 中

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

    // 从预缓存映射表查找 BRK tag，无需读取内存指令。
    // Apple Silicon 上 vm_read_overwrite 对 RX 页面返回 KERN_MEMORY_FAILURE，
    // 且 macOS ESR 中不包含 BRK 立即数。所以最可靠的方式是查缓存。
    bool is_brk = false;
    u32 brk_tag = 0;
    auto cache_it = g_brk_cache.find(pc);
    if (cache_it != g_brk_cache.end()) {
        is_brk = true;
        brk_tag = cache_it->second;
    }

    LOG_INFO("TrapHandler: PC=0x%llx is_brk=%d tag=0x%x", pc, is_brk, brk_tag);

    if (is_brk) {
        u32 tag = brk_tag;

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

// 主线程专用信号栈（32KB）
static std::array<char, 32768> s_main_signal_stack;

// 每线程信号栈 — 使用 mmap 分配页对齐内存
// thread_local std::array 在某些 macOS 版本上可能存在对齐问题
#include <sys/mman.h>
#include <unistd.h>

static constexpr size_t SIGNAL_STACK_SIZE = 0x8000;  // 32KB

Result SetupGuestSignalStack() {
    static thread_local void* s_stack = nullptr;
    if (s_stack) return Result::Success;  // 已设置

    size_t pagesize = (size_t)sysconf(_SC_PAGESIZE);  // 16K on Apple Silicon
    size_t aligned_size = (SIGNAL_STACK_SIZE + pagesize - 1) & ~(pagesize - 1);
    
    void* stack = mmap(nullptr, aligned_size + pagesize,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        LOG_ERROR("SetupGuestSignalStack: mmap failed: %d", errno);
        return Result::OutOfMemory;
    }
    
    // 在底部设置保护页以检测栈溢出
    mprotect(stack, pagesize, PROT_NONE);
    
    s_stack = stack;
    stack_t ss;
    ss.ss_sp = (char*)stack + pagesize;  // 跳过保护页
    ss.ss_size = aligned_size;
    ss.ss_flags = 0;
    
    if (sigaltstack(&ss, nullptr) != 0) {
        LOG_ERROR("SetupGuestSignalStack: sigaltstack failed: %d", errno);
        munmap(stack, aligned_size + pagesize);
        s_stack = nullptr;
        return Result::PermissionDenied;
    }
    LOG_DEBUG("Guest signal stack set up: sp=%p size=0x%zx (thread)", 
              ss.ss_sp, ss.ss_size);
    return Result::Success;
}

void SigHandler::EnsureInstalled() {
    bool expected = false;
    if (!s_installed.compare_exchange_strong(expected, true)) return;

    // 为主线程设置信号栈
    stack_t ss;
    ss.ss_sp = s_main_signal_stack.data();
    ss.ss_size = s_main_signal_stack.size();
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
    LOG_DEBUG("Guest signal handlers installed (global, install-once): main_alt_stack=%p size=%zu",
              s_main_signal_stack.data(), s_main_signal_stack.size());
}

void SigHandler_EnsureInstalled() {
    SigHandler::EnsureInstalled();
}

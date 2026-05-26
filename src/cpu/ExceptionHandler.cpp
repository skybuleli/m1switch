#include "cpu/ExceptionHandler.h"
#include "kernel/SvcTable.h"
#include "common/Log.h"
#include <mach/mach.h>
#include <sys/ucontext.h>

constexpr u32 BRK_MASK = 0xFF00001F;
constexpr u32 BRK_PATTERN = 0xD4200000;
constexpr u32 BRK_TAG_BASE = 0x1000;

std::atomic<bool> SigHandler::s_installed{false};
struct sigaction SigHandler::s_old_action{};

SigHandler::SigHandler() {}
SigHandler::~SigHandler() = default;

void SigHandler::SetSvcDispatch(SvcHandlerFn fn) { dispatch_ = std::move(fn); }

void SigHandler::TrapHandler(int sig, siginfo_t* info, void* uap) {
    (void)sig; (void)info;
    auto* uc = static_cast<ucontext_t*>(uap);
    auto& ss = uc->uc_mcontext->__ss;
    u64 pc = ss.__pc;

    u32 inst;
    vm_size_t rsz;
    vm_read_overwrite(mach_task_self(), pc, sizeof(inst),
                      reinterpret_cast<vm_address_t>(&inst), &rsz);

    if ((inst & BRK_MASK) == BRK_PATTERN) {
        u32 tag = (inst >> 5) & 0xFFFF;
        u32 svc = tag - BRK_TAG_BASE;

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
    }
    ss.__pc = pc + 4;
}

Result SigHandler::Install() {
    EnsureInstalled();
    return Result::Success;
}

void SigHandler::EnsureInstalled() {
    bool expected = false;
    if (!s_installed.compare_exchange_strong(expected, true)) return;

    struct sigaction sa = {};
    sa.sa_sigaction = TrapHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTRAP, &sa, &s_old_action) != 0) {
        LOG_ERROR("sigaction(SIGTRAP) failed");
        s_installed.store(false);
        return;
    }
    LOG_DEBUG("SIGTRAP handler installed (global, install-once)");
}

void SigHandler_EnsureInstalled() {
    SigHandler::EnsureInstalled();
}

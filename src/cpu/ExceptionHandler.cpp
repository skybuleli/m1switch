#include "cpu/ExceptionHandler.h"
#include "common/Log.h"
#include <mach/mach.h>
#include <sys/ucontext.h>

constexpr u32 BRK_MASK = 0xFF00001F;
constexpr u32 BRK_PATTERN = 0xD4200000;
constexpr u32 BRK_TAG_BASE = 0x1000;

ExceptionHandler* ExceptionHandler::s_instance = nullptr;

ExceptionHandler::ExceptionHandler() { if (!s_instance) s_instance = this; }

ExceptionHandler::~ExceptionHandler() {
    if (installed_) sigaction(SIGTRAP, &old_action_, nullptr);
    if (s_instance == this) s_instance = nullptr;
}

void ExceptionHandler::SetSvcDispatch(SvcHandlerFn fn) { dispatch_ = std::move(fn); }

void ExceptionHandler::SigTrapHandler(int sig, siginfo_t* info, void* uap) {
    (void)sig; (void)info;
    auto* uc = static_cast<ucontext_t*>(uap);
    auto& ss = uc->uc_mcontext->__ss;

    u64 pc = ss.__pc;
    u32 inst;
    vm_size_t rsz;
    vm_read_overwrite(mach_task_self(), pc, sizeof(inst),
                      reinterpret_cast<vm_address_t>(&inst), &rsz);

    if ((inst & BRK_MASK) == BRK_PATTERN) {
        u32 tag  = (inst >> 5) & 0xFFFF;
        u32 svc  = tag - BRK_TAG_BASE;

        GuestThreadState gs;
        for (int i = 0; i < 29; i++) gs.x[i] = ss.__x[i];
        gs.x[29] = ss.__fp;  // x29 = FP
        gs.x[30] = ss.__lr;  // x30 = LR
        gs.sp    = ss.__sp;
        gs.pc    = pc;
        // gs.pstate not available on this SDK

        if (s_instance && s_instance->dispatch_)
            s_instance->dispatch_(svc, &gs);

        for (int i = 0; i < 29; i++) ss.__x[i] = gs.x[i];
        ss.__fp  = gs.x[29];
        ss.__lr  = gs.x[30];
        ss.__sp  = gs.sp;
    }

    ss.__pc = pc + 4;
}

Result ExceptionHandler::Install() {
    struct sigaction sa = {};
    sa.sa_sigaction = SigTrapHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTRAP, &sa, &old_action_) != 0) {
        LOG_ERROR("sigaction(SIGTRAP) failed");
        return Result::PermissionDenied;
    }
    installed_ = true;
    LOG_DEBUG("SIGTRAP handler installed");
    return Result::Success;
}

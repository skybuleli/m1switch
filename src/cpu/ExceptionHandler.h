#pragma once

#include "common/Types.h"
#include <signal.h>
#include <functional>

// On ARM64 macOS, BRK #imm generates SIGTRAP.
// Catches it, decodes BRK tag → SVC number,
// dispatches handler, advances PC.

struct __attribute__((aligned(16))) GuestThreadState {
    u64 x[31];   // x0-x30
    u64 sp;
    u64 pc;
};

using SvcHandlerFn = std::function<void(u32 svc_num, GuestThreadState* state)>;

class ExceptionHandler {
public:
    ExceptionHandler();
    ~ExceptionHandler();
    ExceptionHandler(const ExceptionHandler&) = delete;
    ExceptionHandler& operator=(const ExceptionHandler&) = delete;

    void SetSvcDispatch(SvcHandlerFn fn);
    Result Install();

private:
    static void SigTrapHandler(int sig, siginfo_t* info, void* uap);
    static ExceptionHandler* s_instance;

    SvcHandlerFn dispatch_;
    struct sigaction old_action_{};
    bool installed_ = false;
};

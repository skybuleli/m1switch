#pragma once

#include "common/Types.h"
#include <signal.h>
#include <functional>

struct __attribute__((aligned(16))) GuestThreadState {
    u64 x[31];
    u64 sp;
    u64 pc;
};

using SvcHandlerFn = std::function<void(u32 svc_num, GuestThreadState* state)>;

// Renamed to avoid conflict with Mach exception_handler_t typedef
class SigHandler {
public:
    SigHandler();
    ~SigHandler();
    SigHandler(const SigHandler&) = delete;
    SigHandler& operator=(const SigHandler&) = delete;

    void SetSvcDispatch(SvcHandlerFn fn);
    Result Install();

private:
    static void TrapHandler(int sig, siginfo_t* info, void* uap);
    static SigHandler* s_instance;

    SvcHandlerFn dispatch_;
    struct sigaction old_action_{};
    bool installed_ = false;
};

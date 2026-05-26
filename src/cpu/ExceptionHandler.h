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

void SigHandler_EnsureInstalled();

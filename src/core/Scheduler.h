#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include "cpu/ExceptionHandler.h"
#include "kernel/SvcTable.h"
#include <thread>
#include <atomic>
#include <array>
#include <functional>
#include <mutex>

class Scheduler {
public:
    static constexpr int NUM_CORES = 4;
    enum class CoreType { A57_0 = 0, A57_1 = 1, A57_2 = 2, A53 = 3 };

    struct GuestThread {
        u64 entry_point = 0;
        u64 stack_top = 0;
        u64 tls_base = 0;
        bool valid = false;
    };

    Scheduler(Memory& memory);
    ~Scheduler();
    Result Start();
    void Stop();
    bool IsRunning() const;
    int CoreCount() const { return NUM_CORES; }

    void QueueGuestThread(int core_id, const GuestThread& gt);
    void SetSvcDispatch(SvcHandlerFn fn) { svc_dispatch_ = std::move(fn); }

private:
    struct Core {
        std::thread thread;
        std::atomic<bool> running{false};
        std::atomic<u64> cycles{0};
        CoreType type;
        GuestThread guest;
        std::mutex guest_mutex;
    };
    void CoreLoop(Core& core, int core_id);
    Memory& memory_;
    std::array<Core, NUM_CORES> cores_;
    SvcHandlerFn svc_dispatch_;
};

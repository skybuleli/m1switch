#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include "kernel/SvcTable.h"
#include <thread>
#include <atomic>
#include <array>

class Scheduler {
public:
    static constexpr int NUM_CORES = 4;
    enum class CoreType { A57_0 = 0, A57_1 = 1, A57_2 = 2, A53 = 3 };

    Scheduler(Memory& memory);
    ~Scheduler();
    Result Start();
    void Stop();
    bool IsRunning() const;
    int CoreCount() const { return NUM_CORES; }

private:
    struct Core {
        std::thread thread;
        std::atomic<bool> running{false};
        std::atomic<u64> cycles{0};
        CoreType type;
    };
    void CoreLoop(Core& core, int core_id);
    Memory& memory_;
    std::array<Core, NUM_CORES> cores_;
};

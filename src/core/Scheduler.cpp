#include "core/Scheduler.h"

Scheduler::Scheduler(Memory& memory) : memory_(memory) {
    cores_[0].type = CoreType::A57_0;
    cores_[1].type = CoreType::A57_1;
    cores_[2].type = CoreType::A57_2;
    cores_[3].type = CoreType::A53;
}

Scheduler::~Scheduler() { Stop(); }

Result Scheduler::Start() {
    for (int i = 0; i < NUM_CORES; i++) {
        cores_[i].running = true;
        cores_[i].thread = std::thread(&Scheduler::CoreLoop, this,
                                        std::ref(cores_[i]), i);
    }
    LOG_INFO("All %d cores started", NUM_CORES);
    return Result::Success;
}

void Scheduler::Stop() {
    for (auto& core : cores_) core.running = false;
    for (auto& core : cores_) {
        if (core.thread.joinable()) core.thread.join();
    }
}

bool Scheduler::IsRunning() const {
    for (auto& core : cores_) if (core.running) return true;
    return false;
}

void Scheduler::CoreLoop(Core& core, int core_id) {
    char name[32];
    snprintf(name, sizeof(name), "GuestCore%d", core_id);
    pthread_setname_np(name);
    LOG_DEBUG("Core %d started", core_id);

    // Each core runs guest code with SIGTRAP handler
    // (Signal handler installed at process level)

    while (core.running) {
        // Phase 1: stub — in Phase 6+, this will execute guest threads
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        core.cycles++;
    }
}

#include "core/Scheduler.h"

#include <condition_variable>
#include <chrono>

// ── Constructor / Destructor ────────────────────────────────
Scheduler::Scheduler(Memory& memory) : memory_(memory) {
    cores_[0].type = CoreType::A57_0;
    cores_[1].type = CoreType::A57_1;
    cores_[2].type = CoreType::A57_2;
    cores_[3].type = CoreType::A53;

    LOG_INFO("Scheduler created: %d cores", NUM_CORES);
}

Scheduler::~Scheduler() {
    Stop();
}

// ── Start ───────────────────────────────────────────────────
Result Scheduler::Start() {
    all_stopped_ = false;

    for (int i = 0; i < NUM_CORES; i++) {
        cores_[i].running = true;
        cores_[i].thread = std::thread(&Scheduler::CoreLoop, this,
                                        std::ref(cores_[i]), i);

        // Set thread name for debugging
        char name[32];
        snprintf(name, sizeof(name), "GuestCore%d", i);

        // macOS-specific: set thread name from within the thread
        // (will be done inside CoreLoop)
    }

    LOG_INFO("All %d cores started", NUM_CORES);
    return Result::Success;
}

// ── Stop ────────────────────────────────────────────────────
void Scheduler::Stop() {
    all_stopped_ = true;
    for (auto& core : cores_) {
        core.running = false;
        core.exc_handler.Stop();
    }
    for (auto& core : cores_) {
        if (core.thread.joinable()) {
            core.thread.join();
        }
    }
    LOG_INFO("All cores stopped");
}

// ── Is running ──────────────────────────────────────────────
bool Scheduler::IsRunning() const {
    for (auto& core : cores_) {
        if (core.running) return true;
    }
    return false;
}

// ── Core loop ───────────────────────────────────────────────
void Scheduler::CoreLoop(Core& core, int core_id) {
    // Set thread name
    char name[32];
    snprintf(name, sizeof(name), "GuestCore%d", core_id);
    pthread_setname_np(name);

    LOG_INFO("Core %d (%s) started on host thread 0x%x",
             core_id,
             core.type == CoreType::A53 ? "A53" : "A57",
             mach_thread_self());

    // Install exception handler for this thread
    core.exc_handler.InstallOnCurrentThread();

    // Run exception handler loop (blocks here, handling SVC traps)
    core.exc_handler.Run();

    LOG_INFO("Core %d stopped", core_id);
    core.running = false;
}

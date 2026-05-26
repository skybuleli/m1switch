#include "core/Scheduler.h"
#include "cpu/NativeExec.h"

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

void Scheduler::QueueGuestThread(int core_id, const GuestThread& gt) {
    if (core_id < 0 || core_id >= NUM_CORES) return;
    std::lock_guard<std::mutex> lock(cores_[core_id].guest_mutex);
    cores_[core_id].guest = gt;
    LOG_INFO("Queued guest thread on core %d: PC=0x%llx SP=0x%llx",
             core_id, gt.entry_point, gt.stack_top);
}

void Scheduler::CoreLoop(Core& core, int core_id) {
    char name[32];
    snprintf(name, sizeof(name), "GuestCore%d", core_id);
    pthread_setname_np(name);
    LOG_DEBUG("Core %d started", core_id);

    while (core.running) {
        GuestThread gt;
        {
            std::lock_guard<std::mutex> lock(core.guest_mutex);
            gt = core.guest;
        }

        if (gt.valid && gt.entry_point > 0) {
            LOG_INFO("Core %d: running guest PC=0x%llx SP=0x%llx",
                     core_id, gt.entry_point, gt.stack_top);

            if (svc_dispatch_) {
                SigHandler sig;
                sig.SetSvcDispatch(svc_dispatch_);
                sig.Install();
                NativeExec::RunGuest(gt.entry_point, gt.stack_top, gt.tls_base);
            } else {
                LOG_WARN("Core %d: no SVC dispatch, running without handler", core_id);
                NativeExec::RunGuest(gt.entry_point, gt.stack_top, gt.tls_base);
            }

            LOG_INFO("Core %d: guest returned", core_id);
            {
                std::lock_guard<std::mutex> lock(core.guest_mutex);
                core.guest.valid = false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        core.cycles++;
    }
}

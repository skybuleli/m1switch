#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include "core/Scheduler.h"
#include "cpu/ExceptionHandler.h"
#include "gpu/StateTracker.h"
#include "kernel/Kernel.h"
#include "loader/NroLoader.h"

#include <string>
#include <atomic>
#include <mutex>
#include <functional>

class EmulatorCore {
public:
    static constexpr u64 TLS_SLOTS_BASE   = 0xFD000000;
    static constexpr u64 TLS_PER_THREAD   = 0x200;
    static constexpr u64 TLS_IPC_OFFSET   = 0x100;
    static constexpr u64 TLS_IPC_SIZE     = 0x100;

    EmulatorCore();
    ~EmulatorCore();

    Result Initialize();
    Result LoadGame(const std::string& path);
    Result Run();
    void Stop();
    void Pause();
    void Resume();

    bool IsRunning() const { return running_.load(); }
    bool IsPaused() const { return paused_.load(); }

    Memory& GetMemory() { return memory_; }
    Scheduler& GetScheduler() { return scheduler_; }
    StateTracker& GetTracker() { return tracker_; }
    SigHandler& GetSigHandler() { return sig_handler_; }
    KHandleTable& GetHandles() { return handles_; }

    u8* GetTlsIpcBuffer(u64 tls_base) const;

private:
    void InitKernel();
    void InitServices();
    void WireSvcDispatch();

    Memory memory_;
    Scheduler scheduler_;
    StateTracker tracker_;
    SigHandler sig_handler_;
    KHandleTable handles_;

    NroLoadInfo load_info_;
    std::string game_path_;

    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::mutex run_mutex_;
};

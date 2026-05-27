#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <vector>

enum class KObjectType : u8 {
    Invalid = 0,
    Thread,
    Event,
    TransferMemory,
    SharedMemory,
    Session,
    Port,
    InterruptEvent,
    DeviceAddressSpace,
    ResourceLimit,
    CodeMemory,
};

struct Waiter {
    std::mutex* mtx;
    std::condition_variable* cv;
};

class KObject {
public:
    KObjectType type = KObjectType::Invalid;
    u32 handle = 0;
    virtual ~KObject() = default;

    virtual bool IsSignaled() const { return false; }

    void RegisterWaiter(std::mutex* mtx, std::condition_variable* cv) {
        std::lock_guard<std::mutex> lock(waiter_mtx_);
        waiters_.push_back({mtx, cv});
    }

    void UnregisterWaiter(std::condition_variable* cv) {
        std::lock_guard<std::mutex> lock(waiter_mtx_);
        std::erase_if(waiters_, [cv](const Waiter& w) { return w.cv == cv; });
    }

protected:
    void NotifyWaiters() {
        std::lock_guard<std::mutex> lock(waiter_mtx_);
        for (auto& w : waiters_) {
            w.cv->notify_all();
        }
    }

private:
    mutable std::mutex waiter_mtx_;
    std::vector<Waiter> waiters_;
};

class KEvent : public KObject {
public:
    KEvent() { type = KObjectType::Event; }

    void Signal() {
        {
            std::lock_guard<std::mutex> lock(mtx);
            signaled = true;
        }
        cv.notify_all();
        NotifyWaiters();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mtx);
        signaled = false;
    }

    bool IsSignaled() const override {
        std::lock_guard<std::mutex> lock(mtx);
        return signaled;
    }

    Result Wait(s64 timeout_ns) {
        std::unique_lock<std::mutex> lock(mtx);
        if (signaled) return Result::Success;

        if (timeout_ns == 0) return Result::TimedOut;

        if (timeout_ns < 0) {
            cv.wait(lock, [this] { return signaled.load(); });
            return Result::Success;
        }

        auto dur = std::chrono::nanoseconds(timeout_ns);
        if (cv.wait_for(lock, dur, [this] { return signaled.load(); }))
            return Result::Success;
        return Result::TimedOut;
    }

private:
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> signaled{false};
};

class KThread : public KObject {
public:
    KThread() { type = KObjectType::Thread; }

    u64 entry_point = 0;
    u64 stack_top = 0;
    u64 tls_base = 0;
    u64 arg = 0;
    s32 priority = 0x10;
    u32 ideal_core = 0;
    u64 thread_id = 0;
    bool kernel_stack = false;

    std::thread host_thread;
    std::atomic<bool> running{false};
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};

    std::mutex wait_mtx;
    std::condition_variable wait_cv;
    std::atomic<bool> wait_cancel{false};

    KEvent* wake_event = nullptr;

    bool IsSignaled() const override { return finished.load(); }

    void MarkFinished() {
        finished.store(true);
        wait_cv.notify_all();
        NotifyWaiters();
        if (wake_event) {
            wake_event->Signal();
        }
    }

    void WaitUntilFinished() {
        std::unique_lock<std::mutex> lock(wait_mtx);
        wait_cv.wait(lock, [this] { return finished.load(); });
    }
};

class KTransferMemory : public KObject {
public:
    KTransferMemory() { type = KObjectType::TransferMemory; }
    u64 address = 0;
    u64 size = 0;
    Memory::Permission perm = Memory::Permission::None;
};

class KSharedMemory : public KObject {
public:
    KSharedMemory() { type = KObjectType::SharedMemory; }
    u64 address = 0;
    u64 size = 0;
    Memory::Permission perm = Memory::Permission::RW;
    // 物理地址: 对于预分配的共享内存（如 HID），保存实际映射的 guest 地址
    u64 phys_addr = 0;
};

class KInterruptEvent : public KObject {
public:
    KInterruptEvent() { type = KObjectType::InterruptEvent; }
    KEvent event;
};

class KDeviceAddressSpace : public KObject {
public:
    KDeviceAddressSpace() { type = KObjectType::DeviceAddressSpace; }
};

class KResourceLimit : public KObject {
public:
    KResourceLimit() { type = KObjectType::ResourceLimit; }
    s64 limit_values[4] = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};
    s64 current_values[4] = {};
};

class KCodeMemory : public KObject {
public:
    KCodeMemory() { type = KObjectType::CodeMemory; }
    u64 address = 0;
    u64 size = 0;
};

class KSession : public KObject {
public:
    KSession() { type = KObjectType::Session; }
    u32 client_handle = 0;
    u32 server_handle = 0;
};

class KPort : public KObject {
public:
    KPort() { type = KObjectType::Port; }
    u32 client_port = 0;
    u32 server_port = 0;
};

class KHandleTable {
public:
    static constexpr u32 HANDLE_BASE = 0xD000;

    KHandleTable() = default;

    u32 Create(KObject* obj) {
        std::lock_guard<std::mutex> lock(mtx);
        u32 handle = HANDLE_BASE + next_index_++;
        objects_[handle] = obj;
        return handle;
    }

    KObject* Get(u32 handle) const {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = objects_.find(handle);
        return it != objects_.end() ? it->second : nullptr;
    }

    template <typename T>
    T* Get(u32 handle) const {
        KObject* obj = Get(handle);
        return obj ? dynamic_cast<T*>(obj) : nullptr;
    }

    void Close(u32 handle) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = objects_.find(handle);
        if (it != objects_.end()) {
            delete it->second;
            objects_.erase(it);
        }
    }

    void CloseAll() {
        std::lock_guard<std::mutex> lock(mtx);
        for (auto& [h, obj] : objects_) delete obj;
        objects_.clear();
    }

    bool IsValid(u32 handle) const {
        std::lock_guard<std::mutex> lock(mtx);
        return objects_.count(handle) > 0;
    }

private:
    mutable std::mutex mtx;
    std::unordered_map<u32, KObject*> objects_;
    u32 next_index_ = 1;
};

KHandleTable& KernelHandleTable();

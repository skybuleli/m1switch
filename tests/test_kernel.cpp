#include "test_framework.h"
#include "kernel/Kernel.h"
#include "common/Log.h"

TEST(KHandleTable_CreateAndGet) {
    KHandleTable table;

    auto* ev = new KEvent();
    u32 h = table.Create(ev);
    CHECK(h >= KHandleTable::HANDLE_BASE);

    auto* obj = table.Get(h);
    CHECK(obj != nullptr);
    CHECK(obj->type == KObjectType::Event);

    auto* ev2 = table.Get<KEvent>(h);
    CHECK(ev2 != nullptr);
    CHECK(ev2 == ev);

    CHECK(table.Get(0xDEAD) == nullptr);

    table.Close(h);
    CHECK(table.Get(h) == nullptr);

    return true;
}

TEST(KHandleTable_MultipleObjects) {
    KHandleTable table;

    auto* ev = new KEvent();
    auto* th = new KThread();
    auto* tm = new KTransferMemory();

    u32 h1 = table.Create(ev);
    u32 h2 = table.Create(th);
    u32 h3 = table.Create(tm);

    CHECK(h1 != h2);
    CHECK(h2 != h3);

    CHECK(table.Get<KEvent>(h1) == ev);
    CHECK(table.Get<KThread>(h2) == th);
    CHECK(table.Get<KTransferMemory>(h3) == tm);

    CHECK(table.Get<KThread>(h1) == nullptr);

    table.CloseAll();
    CHECK(table.Get(h1) == nullptr);
    CHECK(table.Get(h2) == nullptr);

    return true;
}

TEST(KEvent_SignalAndWait) {
    auto* ev = new KEvent();

    CHECK(!ev->IsSignaled());

    ev->Signal();
    CHECK(ev->IsSignaled());

    Result r = ev->Wait(0);
    CHECK(!Failed(r));

    ev->Clear();
    CHECK(!ev->IsSignaled());

    r = ev->Wait(0);
    CHECK(Failed(r));

    delete ev;
    return true;
}

TEST(KEvent_Timeout) {
    auto* ev = new KEvent();

    auto start = std::chrono::steady_clock::now();
    Result r = ev->Wait(10000000);
    auto end = std::chrono::steady_clock::now();

    CHECK(Failed(r));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    CHECK(elapsed >= 5);

    delete ev;
    return true;
}

TEST(KThread_BasicLifecycle) {
    auto* t = new KThread();
    t->entry_point = 0x40000000;
    t->thread_id = 100;
    t->priority = 0x20;

    CHECK(t->type == KObjectType::Thread);
    CHECK(t->thread_id == 100);
    CHECK(t->priority == 0x20);
    CHECK(!t->started.load());
    CHECK(!t->running.load());
    CHECK(!t->finished.load());

    delete t;
    return true;
}

TEST(KEvent_CrossThreadSignal) {
    auto* ev = new KEvent();
    std::atomic<bool> wait_done{false};

    std::thread waiter([ev, &wait_done]() {
        Result r = ev->Wait(-1);
        wait_done.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!wait_done.load());

    ev->Signal();

    waiter.join();
    CHECK(wait_done.load());

    delete ev;
    return true;
}

TEST(KObject_MultiHandleWait) {
    auto* ev1 = new KEvent();
    auto* ev2 = new KEvent();
    auto* th = new KThread();
    th->started.store(false);

    std::atomic<int> signaled_idx{-1};
    std::mutex wait_mtx;
    std::condition_variable wait_cv;

    std::thread waiter([&]() {
        ev1->RegisterWaiter(&wait_mtx, &wait_cv);
        ev2->RegisterWaiter(&wait_mtx, &wait_cv);
        th->RegisterWaiter(&wait_mtx, &wait_cv);

        std::unique_lock<std::mutex> lock(wait_mtx);
        wait_cv.wait(lock, [&]() {
            if (ev1->IsSignaled()) { signaled_idx.store(0); return true; }
            if (ev2->IsSignaled()) { signaled_idx.store(1); return true; }
            if (th->IsSignaled())  { signaled_idx.store(2); return true; }
            return false;
        });

        ev1->UnregisterWaiter(&wait_cv);
        ev2->UnregisterWaiter(&wait_cv);
        th->UnregisterWaiter(&wait_cv);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(signaled_idx.load() == -1);

    ev2->Signal();

    waiter.join();
    CHECK(signaled_idx.load() == 1);

    delete ev1;
    delete ev2;
    delete th;
    return true;
}

TEST(KThread_MarkFinished) {
    auto* t = new KThread();
    t->thread_id = 200;
    CHECK(!t->IsSignaled());
    CHECK(!t->finished.load());

    std::atomic<bool> wait_done{false};
    std::thread waiter([t, &wait_done]() {
        std::mutex mtx;
        std::condition_variable cv;
        t->RegisterWaiter(&mtx, &cv);
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return t->IsSignaled(); });
        wait_done.store(true);
        t->UnregisterWaiter(&cv);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!wait_done.load());

    t->MarkFinished();
    CHECK(t->IsSignaled());
    CHECK(t->finished.load());

    waiter.join();
    CHECK(wait_done.load());

    delete t;
    return true;
}

TEST(KThread_KernelStackAllocation) {
    auto* t = new KThread();
    t->stack_top = 0;
    t->kernel_stack = true;

    static constexpr u64 STACK_PER_THREAD = 0x100000;
    static constexpr u64 STACK_SLOTS_BASE = 0xFB000000;
    u32 slot = 0;
    t->stack_top = STACK_SLOTS_BASE + (u64)slot * STACK_PER_THREAD + STACK_PER_THREAD;

    CHECK(t->stack_top == STACK_SLOTS_BASE + STACK_PER_THREAD);
    CHECK(t->kernel_stack == true);

    delete t;
    return true;
}

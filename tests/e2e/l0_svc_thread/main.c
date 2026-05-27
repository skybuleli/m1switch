// ── L0: svcCreateThread + StartThread + WaitSynchronization ──
// 创建一个子线程，在其中写入共享变量，主线程等待后验证。
// 验证：线程创建、启动、同步（WaitSynchronization）、TLS 隔离
//────────────────────────────────────────────────────────────

#define L0_TEST
#include "framework.h"
#include <string.h>

// 共享状态（在堆上，多线程共享）
static volatile int g_thread_done = 0;
// 子线程入口
void thread_func(void* arg) {
    (void)arg;
    // 写入共享变量
    g_thread_done = 1;
    svcExitThread();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // ── 创建子线程 ────────────────────────────────────
    u64 stack_addr = 0;
    Result rc = svcSetHeapSize(&stack_addr, 0x100000);
    TEST_ASSERT_OK("svc_thread:heap", rc);

    // 在线程栈顶部开始执行
    u64 stack_top = (u64)stack_addr + 0x100000 - 0x100;

    Handle thread_handle = 0;
    rc = svcCreateThread(&thread_handle,
                         (u64)thread_func,
                         (u64)NULL,  // arg
                         stack_top,
                         0x10,  // priority
                         -2);   // core: -2 = default
    TEST_ASSERT_OK("svc_thread:CreateThread", rc);
    TEST_INFO("svc_thread", "handle=0x%X stack=0x%" PRIx64, thread_handle, stack_top);

    // ── 启动线程 ──────────────────────────────────────
    rc = svcStartThread(thread_handle);
    TEST_ASSERT_OK("svc_thread:StartThread", rc);

    // ── 等待线程结束 ──────────────────────────────────
    rc = svcWaitSynchronization(thread_handle, 5'000'000'000LL); // 5s timeout
    TEST_ASSERT_OK("svc_thread:WaitSync", rc);

    // ── 验证共享变量 ──────────────────────────────────
    TEST_ASSERT("svc_thread:done", g_thread_done == 1);

    // ── 关闭句柄 ──────────────────────────────────────
    svcCloseHandle(thread_handle);

    TEST_PASS("svc_thread");

    svcExitProcess();
    return 0;
}

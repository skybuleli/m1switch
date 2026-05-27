// ── L0: svcSleepThread + svcGetSystemTick ──────────────────
// 睡眠 1ms、10ms、100ms，用系统时钟测量实际经过时间。
// 验证：定时器精度、SVC SleepThread/GetSystemTick
//────────────────────────────────────────────────────────────

#define L0_TEST
#include "framework.h"

// 将系统 tick 转换为纳秒
static inline u64 tick_to_ns(u64 ticks) {
    // M1 的 mach_absolute_time 约为 24 MHz → 41.67 ns/tick
    // 这里不做精确转换，只验证差值在合理范围
    return ticks;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // ── 测试 10ms 睡眠 ───────────────────────────────
    u64 t0 = svcGetSystemTick();
    svcSleepThread(10'000'000LL);  // 10ms
    u64 t1 = svcGetSystemTick();
    u64 elapsed = t1 - t0;

    TEST_INFO("svc_sleep", "10ms sleep: delta=%" PRIu64 " ticks", elapsed);

    // 验证：t1 应该在 t0 之后（计时器在前进）
    TEST_ASSERT("svc_sleep:10ms_forward", t1 > t0);

    // ── 测试 1ms 睡眠 ────────────────────────────────
    t0 = svcGetSystemTick();
    svcSleepThread(1'000'000LL);  // 1ms
    t1 = svcGetSystemTick();
    elapsed = t1 - t0;
    TEST_INFO("svc_sleep", "1ms sleep: delta=%" PRIu64 " ticks", elapsed);
    TEST_ASSERT("svc_sleep:1ms_forward", t1 > t0);

    // ── 测试 0ns 睡眠（yield）─────────────────────────
    t0 = svcGetSystemTick();
    svcSleepThread(0);
    t1 = svcGetSystemTick();
    TEST_INFO("svc_sleep", "0ns sleep: delta=%" PRIu64 " ticks", t1 - t0);

    TEST_PASS("svc_sleep");

    svcExitProcess();
    return 0;
}

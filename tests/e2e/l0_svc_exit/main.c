// ── L0: svcExitProcess ──────────────────────────────────────
// 最简单的测试：直接退出进程。
// 验证：CPU native exec 通路、SVC 分发表、NRO 加载器
//────────────────────────────────────────────────────────────

#define L0_TEST
#include "framework.h"

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // 不做任何初始化——直接退出
    // libnx crt0 会在调用 main 之前运行，如果初始化失败就不会到达这里
    // 所以到达这里本身就验证了 crt0 通路正常
    TEST_PASS("svc_exit");

    svcExitProcess();
    return 0;
}

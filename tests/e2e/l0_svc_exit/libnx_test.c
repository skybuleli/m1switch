// ── 最小 libnx 初始化测试 ────────────────────────────────
// 不覆盖 __appInit，使用 libnx 默认的完整初始化。
// 这样 crt0 会调用 appletInitialize 和 setsysInitialize 等。
//────────────────────────────────────────────────────────────

#include <switch.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    // 如果能到达这里，说明 libnx crt0 初始化成功
    // 通过 OutputDebugString 输出结果
    svcOutputDebugString("TEST_PASS: libnx_init\n", 22);
    svcExitProcess();
    return 0;
}

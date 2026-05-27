// ── L0: svcOutputDebugString ───────────────────────────────
// 向调试通道写入多行文本。验证 debug log 通道和字符串处理。
//────────────────────────────────────────────────────────────

#define L0_TEST
#include "framework.h"
#include <stdio.h>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // 简单的文字输出
    svcOutputDebugString("HELLO from M1Switch test NRO\n", 30);

    // 多行输出
    svcOutputDebugString("LINE1: the quick brown fox\n", 28);
    svcOutputDebugString("LINE2: jumps over the lazy dog\n", 32);

    // 空字符串
    svcOutputDebugString("", 0);

    // 特殊字符
    svcOutputDebugString("SPECIAL: \x01\x02\xFF\x00\x7E\n", 21);

    TEST_PASS("svc_output");

    svcExitProcess();
    return 0;
}

// ── L1: VI 服务 — 显示服务初始化 ───────────────────────────
// 直接测试 vi:u 服务是否可用
#include <switch.h>
#include <stdio.h>
#include <string.h>

void __appInit(void) {
    smInitialize();
    // vi:u 服务通过 smGetService 获取
}

void __appExit(void) {
    smExit();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    Service vi;
    Result rc = smGetService(&vi, "vi:u");
    if (R_SUCCEEDED(rc)) {
        svcOutputDebugString("TEST_PASS: vi_init\n", 19);
        serviceClose(&vi);
    } else {
        svcOutputDebugString("TEST_FAIL: vi_init\n", 19);
    }

    svcExitProcess();
    return 0;
}

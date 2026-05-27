// ── L1: Applet 服务 — 服务可用性测试 ──────────────────────
// 只测试 appletOE: 服务是否可通过 smGetService 获取
// 不调用 appletInitialize（会触发事件等待循环）
#include <switch.h>
#include <stdio.h>
#include <string.h>

void __appInit(void) {
    smInitialize();
}

void __appExit(void) {
    smExit();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    Service srv;
    Result rc = smGetService(&srv, "appletOE:");
    if (R_SUCCEEDED(rc)) {
        svcOutputDebugString("TEST_PASS: applet_basic\n", 24);
        serviceClose(&srv);
    } else {
        svcOutputDebugString("TEST_FAIL: applet_basic\n", 24);
    }

    svcExitProcess();
    return 0;
}

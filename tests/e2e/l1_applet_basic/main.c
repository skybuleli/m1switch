// ── L1: Applet 服务可用性测试 ────────────────────────────
// smGetService("appletOE:") 验证服务存在
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

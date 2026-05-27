// ── L1: smGetService — 服务发现测试 ──────────────────────
// 使用自定义 __appInit 只初始化 SM，不触发 applet/setsys
//────────────────────────────────────────────────────────────

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

    const char* services[] = {
        "sm:", "sm:m:", "vi:u", "vi:m",
        "nvdrv:", "nvmap:", "nvhost-ctrl:",
        "fsp-srv:", "hid:", "set:", "set:sys",
        "apm:", "time:", "time:u", "audout:",
        "appletOE:", "ns:", "ldr:", "spl:",
        "acc:u0", "pcv:"
    };
    int passed = 0, failed = 0;

    for (size_t i = 0; i < sizeof(services)/sizeof(services[0]); i++) {
        Service srv;
        Result rc = smGetService(&srv, services[i]);
        if (R_SUCCEEDED(rc)) {
            passed++;
            serviceClose(&srv);
        } else {
            failed++;
        }
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "TEST_INFO: sm_getsrv %d passed, %d failed\n", passed, failed);
    svcOutputDebugString(buf, strlen(buf));

    if (passed >= 1) {
        svcOutputDebugString("TEST_PASS: sm_getsrv\n", 22);
    } else {
        svcOutputDebugString("TEST_FAIL: sm_getsrv\n", 22);
    }

    svcExitProcess();
    return 0;
}

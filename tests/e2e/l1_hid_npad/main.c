// ── L1: HID 服务 — 服务可用性测试 ─────────────────────────
#include <switch.h>
#include <stdio.h>
#include <string.h>

void __appInit(void) {
    smInitialize();
    // hidInitialize() 由 padConfigureInput 在 main 中调用
}

void __appExit(void) {
    smExit();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    Service hid;
    Result rc = smGetService(&hid, "hid:");
    if (R_SUCCEEDED(rc)) {
        svcOutputDebugString("TEST_PASS: hid_npad\n", 20);
        serviceClose(&hid);
    } else {
        svcOutputDebugString("TEST_FAIL: hid_npad\n", 20);
    }

    svcExitProcess();
    return 0;
}

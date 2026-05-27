// ── L1: Set 服务 — 系统设置读取 ────────────────────────────
#include <switch.h>
#include <stdio.h>
#include <string.h>

void __appInit(void) {
    smInitialize();
    setsysInitialize();
}

void __appExit(void) {
    setsysExit();
    smExit();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    SetSysFirmwareVersion fw;
    Result rc = setsysGetFirmwareVersion(&fw);
    if (R_SUCCEEDED(rc)) {
        char buf[128];
        snprintf(buf, sizeof(buf), "TEST_INFO: firmware %s\n", fw.display_version);
        svcOutputDebugString(buf, strlen(buf));
        svcOutputDebugString("TEST_PASS: set_sys\n", 20);
    } else {
        svcOutputDebugString("TEST_FAIL: set_sys\n", 20);
    }

    svcExitProcess();
    return 0;
}

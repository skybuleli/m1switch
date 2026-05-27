// ── L1: Applet 服务 — 基本生命周期 ─────────────────────────
#include <switch.h>
#include <stdio.h>
#include <string.h>

void __appInit(void) {
    smInitialize();
    appletInitialize();
}

void __appExit(void) {
    appletExit();
    smExit();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    AppletType at = appletGetAppletType();
    bool should_run = appletMainLoop();
    u64 rid = appletGetAppletResourceUserId();

    char buf[128];
    snprintf(buf, sizeof(buf), "TEST_INFO: applet type=%d loop=%d rid=0x%lx\n",
             (int)at, should_run, rid);
    svcOutputDebugString(buf, strlen(buf));
    svcOutputDebugString("TEST_PASS: applet_basic\n", 24);

    svcExitProcess();
    return 0;
}

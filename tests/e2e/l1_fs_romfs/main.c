// ── L1: FS 服务 — RomFS 读取 ───────────────────────────────
// 需要初始化 FS 服务
//────────────────────────────────────────────────────────────

#include <switch.h>
#include <stdio.h>
#include <string.h>

void __appInit(void) {
    smInitialize();
    fsInitialize();
}

void __appExit(void) {
    fsExit();
    smExit();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    Result rc = romfsInit();
    if (R_FAILED(rc)) {
        svcOutputDebugString("TEST_SKIP: fs_romfs (no RomFS)\n", 34);
        svcExitProcess();
    }

    FILE* f = fopen("romfs:/hello.txt", "r");
    if (!f) {
        svcOutputDebugString("TEST_FAIL: fs_romfs (cannot open)\n", 35);
        romfsExit();
        svcExitProcess();
    }

    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf)-1, f);
    fclose(f);
    buf[n] = '\0';

    char out[64];
    snprintf(out, sizeof(out), "TEST_INFO: fs_romfs read %zu bytes\n", n);
    svcOutputDebugString(out, strlen(out));
    svcOutputDebugString("TEST_PASS: fs_romfs\n", 21);

    romfsExit();
    svcExitProcess();
    return 0;
}

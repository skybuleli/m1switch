// ── L1: FS 服务 — RomFS 读取 ───────────────────────────────
// 挂载 RomFS，打开文件 /hello.txt，读取内容并验证校验和。
// 验证：FS 服务 IPC、文件系统 RomFS 加载、文件读取
//────────────────────────────────────────────────────────────
//
// 注意：此测试需要 RomFS 段附加到 NRO。
// 使用 romfs/ 目录中的文件构建：
//   echo "Hello from RomFS!" > romfs/hello.txt
// 构建时会自动嵌入 RomFS。

#include "framework.h"
#include <string.h>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // ── 挂载 RomFS ───────────────────────────────────
    Result rc = romfsInit();
    if (R_FAILED(rc)) {
        TEST_SKIP("fs_romfs", "romfsInit failed — 可能 NRO 没有嵌入 RomFS");
        svcExitProcess(0);
        return 0;
    }

    // ── 打开文件 ──────────────────────────────────────
    FILE* f = fopen("romfs:/hello.txt", "r");
    if (!f) {
        // 先尝试列出根目录
        TEST_FAIL("fs_romfs", "cannot open romfs:/hello.txt");
        romfsExit();
        svcExitProcess();
        return 1;
    }

    // ── 读取内容 ──────────────────────────────────────
    char buf[256];
    size_t nread = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[nread] = '\0';

    TEST_INFO("fs_romfs", "read %zu bytes: '%s'", nread, buf);

    TEST_ASSERT("fs_romfs:nonempty", nread > 0);

    u32 cs = test_checksum(buf, nread);
    TEST_PASS_HASH("fs_romfs", buf, nread);

    romfsExit();
    svcExitProcess();
    return 0;
}

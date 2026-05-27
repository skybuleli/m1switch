// ── L1: Applet 服务 — 基本生命周期 ─────────────────────────
// 初始化 applet，检查 applet 状态、是否在主机模式等。
// 这是许多 libnx 应用启动时的关键路径。
// 验证：Applet (AM) 服务 IPC、appletMainLoop、applet 状态
//────────────────────────────────────────────────────────────

#define L1_TEST
#include "framework.h"

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // ── 初始化 applet ─────────────────────────────────
    Result rc = appletInitialize();
    TEST_ASSERT_OK("applet_basic:init", rc);

    // ── 查询状态 ─────────────────────────────────────
    u64 applet_resource_id = 0;
    rc = appletGetAppletResourceUserId(&applet_resource_id);
    if (R_SUCCEEDED(rc)) {
        TEST_INFO("applet_basic", "resourceUserId=0x%" PRIx64, applet_resource_id);
    } else {
        TEST_INFO("applet_basic", "resourceUserId: unavailable (0x%X)", (unsigned)rc);
    }

    // ── 检查 applet 类型 ─────────────────────────────
    AppletType at = appletGetAppletType();
    TEST_INFO("applet_basic", "appletType=%d", (int)at);

    // ── 检查是否应该运行 ─────────────────────────────
    // 如果 appletMainLoop 返回 false，表示系统要求退出
    bool should_run = appletMainLoop();
    TEST_INFO("applet_basic", "appletMainLoop=%d", should_run);

    // ── 检查是否在主机模式 ──────────────────────────
    bool applet_initialized = appletInitialized();
    TEST_INFO("applet_basic", "appletInitialized=%d", applet_initialized);

    bool running = appletGetAppletState();
    TEST_INFO("applet_basic", "appletGetAppletState=%d", running);

    TEST_PASS("applet_basic");

    appletExit();
    svcExitProcess();
    return 0;
}

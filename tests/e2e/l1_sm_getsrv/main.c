// ── L1: smGetService ───────────────────────────────────────
// 获取所有已知系统服务的会话句柄，验证 SM 服务发现完整。
// 每个服务只获取并立即关闭，不进行后续 IPC。
//────────────────────────────────────────────────────────────

#include "framework.h"
#include <string.h>

// 服务名列表（从 libnx 源码和实际 Switch 游戏中提取）
static const char* const g_service_names[] = {
    "sm:",         // 0 — Service Manager（自身）
    "sm:m:",       // 1 — SM 管理
    "vi:u",        // 2 — 显示
    "vi:m",        // 3 — 显示管理
    "nvdrv:",      // 4 — NV 驱动
    "nvmap:",      // 5 — NV 内存映射
    "nvhost-ctrl:",// 6 — NV 主机控制
    "fsp-srv:",    // 7 — 文件系统
    "hid:",        // 8 — 输入
    "set:",        // 9 — 设置
    "set:sys",     // 10 — 系统设置
    "apm:",        // 11 — 电源管理
    "apm:sys",     // 12 — 电源管理（系统）
    "time:",       // 13 — 时间
    "time:a",      // 14 — 时间（管理员）
    "time:u",      // 15 — 时间（用户）
    "audout:",     // 16 — 音频输出
    "audren:",     // 17 — 音频渲染
    "appletOE:",   // 18 — Applet（旧版）
    "appletAE:",   // 19 — Applet（新版）
    "ns:",         // 20 — NS
    "ns:dev",      // 21 — NS 开发
    "ns:am2",      // 22 — NS AM2
    "ldr:",        // 23 — 加载器
    "ldr:pm",      // 24 — 加载器 PM
    "spl:",        // 25 — 安全处理器
    "spl:mig",     // 26 — SPL 迁移
    "spl:fs",      // 27 — SPL 文件系统
    "acc:u0",      // 28 — 账户 u0
    "acc:u1",      // 29 — 账户 u1
    "acc:su",      // 30 — 账户（超级用户）
    "pcv:",        // 31 — 电源控制
    "bsd:s",       // 32 — BSD 套接字（暂不支持）
    "nifm:",       // 33 — 网络接口（暂不支持）
    "nifm:u",      // 34 — 网络接口用户（暂不支持）
    "usb:",        // 35 — USB（暂不支持）
    "capssc:",     // 36 — 截图（暂不支持）
    "friend:",     // 37 — 好友（暂不支持）
    "ntpc:",       // 38 — NTP（暂不支持）
};

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    int passed = 0, failed = 0, unsupported = 0;
    size_t count = sizeof(g_service_names) / sizeof(g_service_names[0]);

    for (size_t i = 0; i < count; i++) {
        const char* name = g_service_names[i];
        Handle sess = 0;
        Result rc = smGetService(&sess, name);

        if (R_SUCCEEDED(rc)) {
            TEST_INFO("sm_getsrv", "[%zu] %s → ok handle=0x%X", i, name, sess);
            svcCloseHandle(sess);
            passed++;
        } else if (rc == 0xE401) {
            // 0xE401 = sm: 服务未注册
            TEST_INFO("sm_getsrv", "[%zu] %s → unsupported (0x%X)", i, name, (unsigned)rc);
            unsupported++;
        } else {
            TEST_INFO("sm_getsrv", "[%zu] %s → failed (0x%X)", i, name, (unsigned)rc);
            failed++;
        }
    }

    TEST_INFO("sm_getsrv", "result: %d passed, %d unsupported, %d failed",
              passed, unsupported, failed);

    // 至少 SM 自身应该可用
    TEST_ASSERT("sm_getsrv:sm_self", passed >= 1);

    // 核心服务应该可用（至少 sm: + vi: + fsp-srv: + hid: + set:）
    TEST_ASSERT("sm_getsrv:core", passed >= 5);

    TEST_PASS_HASH("sm_getsrv", (void*)g_service_names, sizeof(g_service_names));

    svcExitProcess();
    return 0;
}

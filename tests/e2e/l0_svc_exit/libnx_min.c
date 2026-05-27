// ── 最小 libnx 测试（自定义 __appInit/__appExit）──────────
// 覆盖默认 __appInit，只做必要的服务初始化。
// 这样 crt0 不会调用完整的 applet/setsys/hid/fs 初始化。
//────────────────────────────────────────────────────────────

#include <switch.h>
#include <string.h>

// 最小化初始化：只初始化 SM（Service Manager）
// 不初始化 applet/setsys/hid/fs/time 等
void __appInit(void) {
    // 只初始化 SM——这是几乎所有其他服务的前提
    Result rc = smInitialize();
    if (R_FAILED(rc)) {
        svcOutputDebugString("TEST_FAIL: smInitialize failed\n", 32);
        svcExitProcess();
    }

    // 可选：初始化 applet（只需成功，不需要完整事件循环）
    // 对于大多数 L1 测试，我们可以初始化特定的服务
    // rc = appletInitialize(); 
    // if (R_FAILED(rc)) { ... }
}

void __appExit(void) {
    smExit();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // 如果到达这里，说明自定义 __appInit 成功
    // sm: 服务已就绪，可以测试其他服务了
    
    // 测试 smGetService 是否能获取服务
    Service srv;
    Result rc = smGetService(&srv, "sm:");
    if (R_SUCCEEDED(rc)) {
        serviceClose(&srv);
        svcOutputDebugString("TEST_PASS: libnx_min_init\n", 27);
    } else {
        svcOutputDebugString("TEST_FAIL: smGetService\n", 24);
    }

    svcExitProcess();
    return 0;
}

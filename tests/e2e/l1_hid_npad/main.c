// ── L1: HID 服务 — Npad 手柄读取 ───────────────────────────
// 初始化 HID 和 Npad，读取当前输入状态（非阻塞）。
// 验证：HID 服务 IPC、共享内存布局、Npad 初始化
//────────────────────────────────────────────────────────────

#define L1_TEST
#include "framework.h"
#include <string.h>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // ── 初始化 HID ───────────────────────────────────
    Result rc = hidInitialize();
    TEST_ASSERT_OK("hid_npad:init", rc);

    // ── 配置输入 ──────────────────────────────────────
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    // ── 初始化默认手柄 ───────────────────────────────
    PadState pad;
    padInitializeDefault(&pad);

    // ── 读取一次状态（不阻塞）────────────────────────
    padUpdate(&pad);
    u64 buttons = padGetButtons(&pad);
    HidNpadStyleTag style = padGetStyle(&pad);

    TEST_INFO("hid_npad", "style=0x%X buttons=0x%" PRIx64, (unsigned)style, buttons);

    // ── 读取摇杆 ──────────────────────────────────────
    HidAnalogStickState ls = padGetStickPos(&pad, 0);  // LEFT
    HidAnalogStickState rs = padGetStickPos(&pad, 1);  // RIGHT
    TEST_INFO("hid_npad", "left_stick: x=%d y=%d", ls.x, ls.y);
    TEST_INFO("hid_npad", "right_stick: x=%d y=%d", rs.x, rs.y);

    // ── 验证摇杆在中心附近 ──────────────────────────
    // 初始状态摇杆应该在 (0,0) 附近
    TEST_ASSERT("hid_npad:ls_x", ls.x >= -100 && ls.x <= 100);
    TEST_ASSERT("hid_npad:ls_y", ls.y >= -100 && ls.y <= 100);

    TEST_PASS("hid_npad");

    hidExit();
    svcExitProcess();
    return 0;
}

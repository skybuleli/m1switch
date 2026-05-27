// ── L1: Set 服务 — 系统设置读取 ────────────────────────────
// 通过 set:sys 服务读取系统固件版本、语言代码、序列号等设置。
// 验证：Set 服务 IPC、系统设置查询
//────────────────────────────────────────────────────────────

#include "framework.h"
#include <string.h>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // ── 初始化 setsys ─────────────────────────────────
    Result rc = setsysInitialize();
    TEST_ASSERT_OK("set_sys:init", rc);
    TEST_INFO("set_sys", "setsys initialized");

    // ── 读取固件版本 ─────────────────────────────────
    SetSysFirmwareVersion fw;
    rc = setsysGetFirmwareVersion(&fw);
    if (R_SUCCEEDED(rc)) {
        TEST_INFO("set_sys", "firmware: %s (major=%d minor=%d micro=%d)",
                  fw.display_version, fw.major, fw.minor, fw.micro);
    } else {
        TEST_INFO("set_sys", "firmware version: unavailable (0x%X)", (unsigned)rc);
    }

    // ── 读取语言代码 ─────────────────────────────────
    u64 language_code = 0;
    rc = setsysGetLanguageCode(&language_code);
    if (R_SUCCEEDED(rc)) {
        char lang_str[9] = {};
        memcpy(lang_str, &language_code, 8);
        TEST_INFO("set_sys", "language_code: 0x%016" PRIx64 " ('%s')",
                  language_code, lang_str);
    } else {
        TEST_INFO("set_sys", "language: unavailable (0x%X)", (unsigned)rc);
    }

    // ── 读取颜色方案 ─────────────────────────────────
    u32 color_set = 0;
    rc = setsysGetColorSetId(&color_set);
    if (R_SUCCEEDED(rc)) {
        TEST_INFO("set_sys", "color_set: %u", color_set);
    }

    // ── 读取区域 ─────────────────────────────────────
    SetSysRegion region;
    rc = setsysGetRegionCode(&region);
    if (R_SUCCEEDED(rc)) {
        TEST_INFO("set_sys", "region: %d", (int)region);
    }

    // 至少初始化要成功
    TEST_PASS("set_sys");

    setsysExit();
    svcExitProcess();
    return 0;
}

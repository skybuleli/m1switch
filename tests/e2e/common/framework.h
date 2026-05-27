// ── M1Switch E2E Test Framework ─────────────────────────────
// 统一的测试宏集合。
//
// 测试分层：
//   L0 — 裸 syscall，通过 __appInit 重载跳过 libnx 初始化
//   L1 — 单服务测试，使用完整的 libnx 初始化
//
// 输出格式（volt-agent 可以验证 output_contains）:
//   TEST_PASS: <name> [hash=0xABCD1234]
//   TEST_FAIL: <name> [reason=<msg>]
//   TEST_SKIP: <name> [reason=<msg>]
//   TEST_INFO: <name> <message>
//────────────────────────────────────────────────────────────

#pragma once
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── L0 测试用：覆盖 libnx 的 __appInit/__appExit ───────────
// libnx crt0 在 main() 之前调用 __appInit()。
// 默认版本尝试初始化 applet/am/set 等完整服务栈。
// L0 测试只需要 SVC 通路，重载为最小初始化以避免 crt0 崩溃。
//
// 用法：在 L0 测试的 main.c 中定义
//   #define L0_TEST  // 必须在 #include "framework.h" 之前

#ifdef L0_TEST

// 覆盖 libnx 默认 __appInit/__appExit。
// libnx crt0 在 main() 前调用 __appInit() 初始化 applet/am/set 等。
// L0 测试不需要这些，重载为空避免 crt0 崩溃。
// 注意：必须用 **强符号**（不要 __attribute__((weak))）来覆盖 libnx.a 中的弱符号
void __appInit(void) {
    // L0 测试：不做任何服务初始化
}

void __appExit(void) {
    // no-op
}

// 确保 libnx 不尝试调用任何服务
// 如果链接器报了 undefined reference 到 __appInit，说明这个覆盖生效了
__attribute__((used)) static const char* __nx_appinit_type = "L0_MINIMAL";

#endif

// ── 简单的 adler32 校验和 ──────────────────────────────────
static inline u32 test_checksum(const void* data, size_t len) {
    u32 a = 1, b = 0;
    const u8* p = (const u8*)data;
    for (size_t i = 0; i < len; i++) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// ── 输出宏 ───────────────────────────────────────────────────

#define TEST_PASS(name) do { \
    svcOutputDebugString("TEST_PASS: " name "\n", sizeof("TEST_PASS: " name "\n") - 1); \
} while(0)

#define TEST_PASS_HASH(name, data, len) do { \
    u32 _h = test_checksum(data, len); \
    char _buf[128]; \
    snprintf(_buf, sizeof(_buf), "TEST_PASS: %s hash=0x%08X\n", name, _h); \
    svcOutputDebugString(_buf, strlen(_buf)); \
} while(0)

#define TEST_FAIL(name, fmt, ...) do { \
    char _buf[256]; \
    snprintf(_buf, sizeof(_buf), "TEST_FAIL: " name " [reason=" fmt "]\n", ##__VA_ARGS__); \
    svcOutputDebugString(_buf, strlen(_buf)); \
} while(0)

#define TEST_SKIP(name, reason) do { \
    char _buf[256]; \
    snprintf(_buf, sizeof(_buf), "TEST_SKIP: " name " [reason=" reason "]\n"); \
    svcOutputDebugString(_buf, strlen(_buf)); \
} while(0)

#define TEST_INFO(name, fmt, ...) do { \
    char _buf[256]; \
    snprintf(_buf, sizeof(_buf), "TEST_INFO: " name " " fmt "\n", ##__VA_ARGS__); \
    svcOutputDebugString(_buf, strlen(_buf)); \
} while(0)

// ── 断言宏 ───────────────────────────────────────────────────

#define TEST_ASSERT(name, cond) do { \
    if (!(cond)) { \
        TEST_FAIL(name, "assertion failed: %s", #cond); \
        svcExitProcess(); \
    } \
} while(0)

#define TEST_ASSERT_EQ(name, a, b, fmt) do { \
    if ((a) != (b)) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), \
            "TEST_FAIL: " name " [assert_eq: " #a "=" fmt " != " #b "=" fmt "]\n", \
            (a), (b)); \
        svcOutputDebugString(_buf, strlen(_buf)); \
        svcExitProcess(); \
    } \
} while(0)

#define TEST_ASSERT_OK(name, result) do { \
    if (R_FAILED(result)) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), \
            "TEST_FAIL: " name " [result=0x%X module=0x%X desc=0x%X]\n", \
            (unsigned)(result), \
            (unsigned)R_MODULE(result), \
            (unsigned)R_DESCRIPTION(result)); \
        svcOutputDebugString(_buf, strlen(_buf)); \
        svcExitProcess(); \
    } \
} while(0)

// ── 内存工具 ─────────────────────────────────────────────────

static inline u32 test_fill_pattern(void* buf, size_t len, u8 pattern) {
    memset(buf, pattern, len);
    return test_checksum(buf, len);
}

static inline bool test_verify_pattern(const void* buf, size_t len, u8 pattern) {
    const u8* p = (const u8*)buf;
    for (size_t i = 0; i < len; i++) {
        if (p[i] != pattern) return false;
    }
    return true;
}

#ifdef __cplusplus
}
#endif

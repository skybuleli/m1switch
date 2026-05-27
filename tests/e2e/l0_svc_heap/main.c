// ── L0: svcSetHeapSize + 内存读写 ──────────────────────────
// 分配堆、写入模式、回读验证、返回校验和
// 验证：SVC SetHeapSize、内存映射、guest -> host 地址转换
//────────────────────────────────────────────────────────────

#define L0_TEST
#include "framework.h"
#include <string.h>

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // ── 分配堆 ────────────────────────────────────────
    u64 heap_addr = 0;
    Result rc = svcSetHeapSize(&heap_addr, 0x200000); // 2 MB
    TEST_ASSERT_OK("svc_heap:SetHeapSize", rc);
    TEST_INFO("svc_heap", "heap at 0x%" PRIx64 " size=0x200000", heap_addr);

    // ── 写入模式 0xAA ─────────────────────────────────
    memset((void*)heap_addr, 0xAA, 0x200000);
    u32 cs1 = test_checksum((void*)heap_addr, 0x200000);
    TEST_INFO("svc_heap", "pattern 0xAA checksum=0x%08X", cs1);

    // ── 写入模式 0x55 ─────────────────────────────────
    memset((void*)heap_addr, 0x55, 0x200000);
    u32 cs2 = test_checksum((void*)heap_addr, 0x200000);
    TEST_INFO("svc_heap", "pattern 0x55 checksum=0x%08X", cs2);

    // ── 验证内容（抽样） ──────────────────────────────
    bool ok = true;
    u8* p = (u8*)heap_addr;
    for (size_t i = 0; i < 0x200000; i += 0x1000) {
        if (p[i] != 0x55) { ok = false; break; }
    }
    TEST_ASSERT("svc_heap:content", ok);

    // ── 校验和不同说明写入生效 ────────────────────────
    TEST_ASSERT("svc_heap:checksum_diff", cs1 != cs2);

    TEST_PASS_HASH("svc_heap", (void*)heap_addr, 0x200000);

    svcExitProcess();
    return 0;
}

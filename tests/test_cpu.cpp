// ── CPU / SVC tests ────────────────────────────────────────

#include <cstring>
#include "cpu/NativeExec.h"
#include "kernel/SvcTable.h"

TEST(SVC_Patching) {
    u8 code[32] = {};
    auto emit = [&](int off, u32 inst) { std::memcpy(code + off, &inst, 4); };

    emit(0,  0xD503201F);  // NOP
    emit(4,  0xD4000021);  // SVC #0x01
    emit(8,  0xD40000E1);  // SVC #0x07 (svcExitProcess)
    emit(12, 0xD503201F);  // NOP
    emit(16, 0xD4000D61);  // SVC #0x6B

    std::vector<std::pair<u32, u32>> svc_map;
    CHECK_EQ(Result::Success, NativeExec::PatchSVCs(code, 20, svc_map));
    CHECK_EQ(3, svc_map.size());

    // Verify mappings
    CHECK_EQ(0x1001, svc_map[0].first);   // BRK tag
    CHECK_EQ(0x01,   svc_map[0].second);   // SVC #
    CHECK_EQ(0x1007, svc_map[1].first);
    CHECK_EQ(0x07,   svc_map[1].second);

    // NOPs should be unchanged
    u32 inst0, inst3;
    std::memcpy(&inst0, code, 4);
    std::memcpy(&inst3, code + 12, 4);
    CHECK_EQ(0xD503201F, inst0);
    CHECK_EQ(0xD503201F, inst3);

    return true;
}

// SVC handler recording
static int g_test_svc_count = 0;
static u32 g_test_svc_last = 0xFFFF;
static void TestSvcHandler(u32 num, GuestThreadState*) {
    g_test_svc_count++;
    g_test_svc_last = num;
}

TEST(SVC_Dispatch) {
    SvcTable_Init();

    g_test_svc_count = 0;
    SvcTable_Register(0x07, TestSvcHandler);

    GuestThreadState state = {};
    state.pc = 0x40000000;
    state.x[0] = 42;

    SvcHandler_Dispatch(0x07, &state);

    CHECK_EQ(1, g_test_svc_count);
    CHECK_EQ(0x07, g_test_svc_last);

    return true;
}

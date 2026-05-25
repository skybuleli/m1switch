// ── Phase 1 Integration Test ────────────────────────────────
// Tests NRO loading, SVC patching, memory mapping.
//
// To run: ./build/tests/integration/test_nro_loading
// Requires a test NRO at tests/fixtures/nro/minimal.nro
//
// Phase 1: compile-only verification.
// Full test requires a known homebrew binary.

#include "common/Log.h"
#include "common/Types.h"
#include "memory/Memory.h"
#include "loader/NroLoader.h"
#include "loader/NpdmParser.h"
#include "cpu/NativeExec.h"
#include "kernel/SvcTable.h"

// Test 1: Memory allocation and basic access
bool Test_Memory() {
    Log::Init();

    Memory memory;

    // Allocate a page
    Result r = memory.MapPhysical(0x40000000, 0x1000,
                                   Memory::Permission::RW);
    if (Failed(r)) {
        LOG_ERROR("Test_Memory: MapPhysical failed");
        return false;
    }

    // Write and read back
    r = memory.Write<u32>(0x40000000, 0xDEADBEEF);
    if (Failed(r)) return false;

    u32 val = 0;
    r = memory.Read<u32>(0x40000000, &val);
    if (Failed(r) || val != 0xDEADBEEF) {
        LOG_ERROR("Test_Memory: readback mismatch: got 0x%08x", val);
        return false;
    }

    LOG_INFO("Test_Memory: PASSED");
    return true;
}

// Test 2: SVC patching
bool Test_SvcPatching() {
    // Create a small code buffer with a few SVC instructions
    u8 code[32] = {};
    auto emit_inst = [&](int offset, u32 inst) {
        std::memcpy(code + offset, &inst, 4);
    };

    // ARM64 NOP
    emit_inst(0,  0xD503201F);
    // SVC #0x01 (svcSetHeapSize)
    emit_inst(4,  0xD4000021);
    // SVC #0x07 (svcExitProcess)
    emit_inst(8,  0xD40000E1);
    // ARM64 NOP
    emit_inst(12, 0xD503201F);
    // SVC #0x6B (svcOutputDebugString)
    emit_inst(16, 0xD4000D61);

    std::vector<std::pair<u32, u32>> svc_map;
    Result r = NativeExec::PatchSVCs(code, 20, svc_map);
    if (Failed(r) || svc_map.size() != 3) {
        LOG_ERROR("Test_SvcPatching: expected 3 patches, got %zu",
                  svc_map.size());
        return false;
    }

    // Verify the patches
    // Each BRK tag should be 0x1000 + SVC number
    auto check = [&](int idx, u32 expected_svc) {
        auto [tag, svc] = svc_map[idx];
        if (svc != expected_svc || tag != 0x1000 + expected_svc) {
            LOG_ERROR("Test_SvcPatching: entry %d: svc=%u tag=0x%x",
                      idx, svc, tag);
            return false;
        }
        return true;
    };

    if (!check(0, 0x01) || !check(1, 0x07) || !check(2, 0x6B)) {
        return false;
    }

    // Verify NOP instructions were NOT patched
    u32 inst0, inst3;
    std::memcpy(&inst0, code, 4);
    std::memcpy(&inst3, code + 12, 4);
    if (inst0 != 0xD503201F || inst3 != 0xD503201F) {
        LOG_ERROR("Test_SvcPatching: NOPs were incorrectly modified");
        return false;
    }

    LOG_INFO("Test_SvcPatching: PASSED");
    return true;
}

int main(int argc, char** argv) {
    Log::Init();

    bool all_pass = true;
    all_pass &= Test_Memory();
    all_pass &= Test_SvcPatching();

    if (all_pass) {
        LOG_INFO("=== All Phase 1 tests PASSED ===");
        return 0;
    } else {
        LOG_ERROR("=== Some Phase 1 tests FAILED ===");
        return 1;
    }
}

// ═══════════════════════════════════════════════════════════
// E2E Boot Test: Full emulator boot + guest execution
//
// 1. Initialize Memory (4GB UMA)
// 2. Create minimal NRO with ARM64 code
// 3. Load NRO into guest memory
// 4. Patch SVC instructions to BRK
// 5. Setup stack
// 6. Install SIGTRAP handler + SVC dispatch table
// 7. Execute guest code via NativeExec::RunGuest
// 8. Verify SVC handlers fire correctly
// ═══════════════════════════════════════════════════════════

#include "../test_framework.h"
#include "common/Log.h"
#include "common/Types.h"
#include "memory/Memory.h"
#include "loader/NroLoader.h"
#include "cpu/NativeExec.h"
#include "cpu/ExceptionHandler.h"
#include "cpu/Debugger.h"
#include "kernel/SvcTable.h"
#include "kernel/Kernel.h"
#include "gpu/StateTracker.h"
#include "services/Nv.h"
#include "services/Ipc.h"

#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

// ── ARM64 instruction helpers ──────────────────────────────
constexpr u32 SVC(u32 i)  { return 0xD4000001 | ((i & 0xFFFF) << 5); }
constexpr u32 MOVZ(u32 rd, u32 imm, u32 sh) {
    return 0xD2800000 | ((imm & 0xFFFF) << 5)
           | ((sh & 3) << 21) | (rd & 0x1F);
}
constexpr u32 MOVK(u32 rd, u32 imm, u32 sh) {
    return 0xF2800000 | ((imm & 0xFFFF) << 5)
           | ((sh & 3) << 21) | (rd & 0x1F);
}
constexpr u32 ADD(u32 rd, u32 rn, u32 imm) {
    return 0x91000000 | (imm << 10) | (rn << 5) | rd;
}
constexpr u32 STR(u32 rt, u32 rn, u32 off) {
    return 0xF9000000 | ((off >> 3) << 10) | (rn << 5) | rt;
}
constexpr u32 LDR(u32 rt, u32 rn, u32 off) {
    return 0xF9400000 | ((off >> 3) << 10) | (rn << 5) | rt;
}
constexpr u32 B_INF = 0x14000000;

// ── Test NRO builder ──────────────────────────────────────
static std::vector<u8> MakeTestNRO() {
    // Program: set x0=42 → SVC 0x07 (svcExitProcess) → infinite loop
    constexpr u32 prog[] = {
        MOVZ(0, 42, 0),      // x0 = 42 (exit code)
        SVC(0x07),            // svcExitProcess(42)
        B_INF,                // infinite loop (safety)
    };
    constexpr u32 code_sz = sizeof(prog);
    constexpr u32 a_code = (code_sz + 0xFFF) & ~0xFFF;

    std::vector<u8> nro(0x100 + a_code, 0);
    auto st = [&](u32 o, u32 v) {
        nro[o+0]=(v>>0)&0xFF; nro[o+1]=(v>>8)&0xFF;
        nro[o+2]=(v>>16)&0xFF; nro[o+3]=(v>>24)&0xFF;
    };

    // 16-byte preamble: B #0x10 (skip past header)
    st(0x00, 0x14000004);
    // NRO0 header at offset 0x10
    st(0x10, 0x304F524E);                          // magic "NRO0"
    st(0x14, 0);                                   // version
    st(0x18, (u32)nro.size());                     // size
    st(0x1C, 0);                                   // flags
    st(0x20, 0x110);                               // text_start (file offset)
    st(0x24, code_sz);                             // text_size
    st(0x28, 0x110 + a_code);                      // rodata_start
    st(0x2C, 0);                                   // rodata_size
    st(0x30, 0x110 + a_code);                      // data_start
    st(0x34, 0);                                   // data_size
    st(0x38, 0);                                   // bss_size
    st(0x3C, 0);                                   // reserved
    std::memcpy(&nro[0x40], "M1SW-E2E", 8);        // build_id at header_off+0x30

    // Code at offset 0x110
    std::memcpy(&nro[0x110], prog, sizeof(prog));
    return nro;
}

// ── E2E Boot Test globals ────────────────────────────────
static std::atomic<u64> g_last_svc{0xFFFFFFFF};
static std::atomic<u64> g_exit_code{0};
static std::atomic<bool> g_exit_called{false};

static void E2E_ExitHandler(u32 num, GuestThreadState* state) {
    g_last_svc.store(num);
    g_exit_code.store(state->x[0]);
    g_exit_called.store(true);
    state->x[0] = 0;
}

TEST(E2E_FullBoot) {
    Log::Init();
    LOG_INFO("=== E2E: Full Boot Test ===");

    g_last_svc.store(0xFFFFFFFF);
    g_exit_code.store(0);
    g_exit_called.store(false);

    // ── 1. Memory ──────────────────────────────────
    Memory memory;
    CHECK(memory.BaseAddress() != 0);

    // ── 2. StateTracker + NV wiring ─────────────────
    StateTracker tracker;
    tracker.SetMemory(&memory);
    ServiceNv_SetMemory(&memory);
    ServiceNv_SetTracker(&tracker);
    // GPFifo 被 StateTracker 内部持有，NV service 通过 StateTracker 间接访问

    // ── 3. Load NRO ────────────────────────────────
    auto nro = MakeTestNRO();
    NroLoader loader(memory);
    NroLoadInfo info;
    Result r = loader.LoadFromBuffer(nro, info);
    CHECK(!Failed(r));
    CHECK(info.entry_point != 0);
    CHECK(!info.segments.empty());
    LOG_INFO("NRO loaded: entry=0x%llx, %zu segments",
             info.entry_point, info.segments.size());

    // ── 4. 验证 SVC 已被加载器自动打补丁 ──────────
    // NroLoader::LoadFromBuffer 内部已调用 PatchSVCs 并将 .text 保护为 RX
    // 这里验证 SVC #0（偏移 4 字节处）已被替换为 BRK
    auto& seg = info.segments[0];
    u8* text = memory.Pointer(seg.guest_address);
    CHECK(text != nullptr);

    // 读取偏移 4 处的已打补丁指令（SVC #0x07 应被替换为 BRK #0x1007）
    u32 patched_svc;
    std::memcpy(&patched_svc, text + 4, sizeof(patched_svc));
    // SVC #0x07 原始 = 0xD4000001 | (0x07 << 5) = 0xD40000E1
    // 补丁后 = BRK #0x1007 = 0xD4200000 | (0x1007 << 5) = 0xD42200E0
    CHECK_EQ(patched_svc, (u32)0xD42200E0);
    LOG_INFO("SVC 补丁验证通过: 偏移4指令 = 0x%08x (应为 BRK #0x1007)", patched_svc);

    // ── 5. Stack ───────────────────────────────────
    memory.SetupStack(0x100000);
    CHECK(memory.GetStackTop() > 0);

    // ── 6. SVC table + Debugger ────────────────────
    SvcTable_Init();
    auto& dbg = GlobalDebugger();
    dbg.SetMemory(&memory);
    SvcTable_Register(0x07, E2E_ExitHandler);

    // ── 7. Install SIGTRAP handler ──────────────────
    SigHandler sig_handler;
    sig_handler.SetSvcDispatch([](u32 svc_num, GuestThreadState* gs) {
        SvcHandler_Dispatch(svc_num, gs);
    });
    sig_handler.Install();
    LOG_INFO("SIGTRAP handler installed");

    // ── 8. Run guest in separate thread ─────────────
    u64 abs_entry = memory.BaseAddress() + info.entry_point;
    u64 abs_stack = memory.BaseAddress() + memory.GetStackTop();

    std::thread guest([abs_entry, abs_stack]() {
        NativeExec::RunGuest(abs_entry, abs_stack, 0);
    });

    // ── 9. Wait for exit or timeout ────────────────
    int waited = 0;
    while (!g_exit_called.load() && waited < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited++;
    }

    guest.detach();

    // ── 10. Verify ─────────────────────────────────
    CHECK(g_exit_called.load());
    CHECK_EQ(g_exit_code.load(), (u64)42);
    CHECK_EQ(g_last_svc.load(), (u64)0x07);
    LOG_INFO("=== E2E Boot PASSED: svcExitProcess(%llu) ===",
             g_exit_code.load());
    return true;
}

// ── E2E Debugger Breakpoint Test ──────────────────────────
static std::atomic<bool> g_bp_hit{false};
static u64 g_bp_addr = 0;

static void E2E_BpCallback(u64 addr, const CpuRegisterSnapshot&) {
    if (addr == g_bp_addr) g_bp_hit.store(true);
}

TEST(E2E_DebuggerBreakpoint) {
    Log::Init();
    LOG_INFO("=== E2E: Debugger Breakpoint Test ===");

    g_bp_hit.store(false);

    Memory memory;
    CHECK(memory.BaseAddress() != 0);

    auto nro = MakeTestNRO();
    NroLoader loader(memory);
    NroLoadInfo info;
    Result r = loader.LoadFromBuffer(nro, info);
    CHECK(!Failed(r));

    auto& seg = info.segments[0];
    u8* text = memory.Pointer(seg.guest_address);
    CHECK(text != nullptr);
    // 加载器已自动完成 SVC 补丁，无需再次调用 PatchSVCs
    memory.SetupStack(0x100000);

    SvcTable_Init();
    auto& dbg = GlobalDebugger();
    dbg.SetMemory(&memory);

    // 在入口地址设置断点（使用主机绝对地址，匹配信号处理器的 PC）
    g_bp_addr = memory.BaseAddress() + info.entry_point;
    dbg.SetBreakpoint(g_bp_addr);
    CHECK(dbg.HasBreakpoint(g_bp_addr));
    CHECK(dbg.GetBreakpointCount() == 1);

    dbg.SetBreakpointCallback(E2E_BpCallback);

    SigHandler sig_handler;
    sig_handler.SetSvcDispatch([](u32 svc_num, GuestThreadState* gs) {
        SvcHandler_Dispatch(svc_num, gs);
    });
    sig_handler.Install();

    u64 abs_entry = g_bp_addr;
    u64 abs_stack = memory.BaseAddress() + memory.GetStackTop();

    std::thread guest([abs_entry, abs_stack]() {
        NativeExec::RunGuest(abs_entry, abs_stack, 0);
    });

    int waited = 0;
    while (!g_bp_hit.load() && waited < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited++;
    }

    guest.detach();
    dbg.ClearAllBreakpoints();

    CHECK(g_bp_hit.load());
    CHECK(dbg.IsPaused());
    CpuRegisterSnapshot regs = dbg.GetLastRegisters();
    CHECK(regs.pc == g_bp_addr);

    LOG_INFO("=== E2E Breakpoint PASSED: hit at 0x%llx ===", g_bp_addr);
    return true;
}

int main() {
    Log::Init();
    return RunAllTests();
}

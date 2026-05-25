// Phase 1: End-to-End Boot Test
// Loads a minimal NRO, patches SVCs, starts guest + handler threads,
// verifies svcExitProcess fires.

#include "common/Log.h"
#include "common/Types.h"
#include "memory/Memory.h"
#include "loader/NroLoader.h"
#include "loader/NpdmParser.h"
#include "cpu/NativeExec.h"
#include "cpu/ExceptionHandler.h"
#include "kernel/SvcTable.h"

#include <vector>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>

constexpr u32 SVC(u32 i) { return 0xD4000001 | ((i & 0xFFFF) << 5); }
constexpr u32 MOVZ(u32 rd, u32 imm, u32 sh) {
    return 0xD2800000 | ((imm & 0xFFFF) << 5) | ((sh & 3) << 21) | (rd & 0x1F);
}
constexpr u32 B_INF = 0x14000000;

static std::vector<u8> MakeNRO() {
    constexpr u32 prog[] = { MOVZ(0, 0, 0), SVC(0x07), B_INF };
    constexpr u32 code_sz = sizeof(prog);
    constexpr u32 align  = 0x1000;
    constexpr u32 a_code = (code_sz + align - 1) & ~(align - 1);
    u8 nro[0x100 + a_code] = {};
    auto st = [&](u32 o, u32 v) {
        nro[o+0]=(v>>0)&0xFF; nro[o+1]=(v>>8)&0xFF;
        nro[o+2]=(v>>16)&0xFF; nro[o+3]=(v>>24)&0xFF;
    };
    st(0x00, 0x304F524E); st(0x04, 0); st(0x08, sizeof(nro));
    st(0x0C, 0); st(0x10, 0x100); st(0x14, code_sz);
    std::memcpy(&nro[0x30], "M1SW-E2E", 8);
    st(0x100, prog[0]); st(0x104, prog[1]); st(0x108, prog[2]);
    return {nro, nro + sizeof(nro)};
}

static std::atomic<int>  g_svc{0};
static std::atomic<bool> g_exit{false};

void MySvcHandler(u32 num, arm_unified_thread_state* st) {
    g_svc++;
    if (num == 0x07) {
        LOG_INFO("svcExitProcess(0) — BOOT OK!");
        g_exit = true;
    }
    st->ts_64.__x[0] = 0;
}

int main() {
    Log::Init();
    LOG_INFO("=== Phase 1 E2E Boot Test ===");

    Memory mem;
    auto nro = MakeNRO();

    NroLoader ld(mem);
    NroLoadInfo info;
    if (Failed(ld.LoadFromBuffer(nro, info))) { LOG_FATAL("NRO fail"); return 1; }

    auto& seg = info.segments[0];
    u8* text = mem.Pointer(seg.guest_address);
    std::vector<std::pair<u32,u32>> svc_map;
    NativeExec::PatchSVCs(text, seg.size, svc_map);

    mem.SetupStack(0x100000);
    SvcTable_Init();
    SvcTable_Register(0x07, MySvcHandler);

    LOG_INFO("Starting handler thread...");

    // Handler thread — listens for Mach exceptions on its own port
    MachExceptionHandler handler;
    handler.SetSvcDispatch([](u32 tag, u32 svc, auto* state) {
        SvcHandler_Dispatch(svc, state);
    });

    // Install handler's port as task-level exception handler
    // (catches EXC_BREAKPOINT on all threads)
    kern_return_t kr = task_set_exception_ports(
        mach_task_self(), EXC_MASK_BREAKPOINT, handler.Port(),
        EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, ARM_EXCEPTION_STATE64);
    if (kr != KERN_SUCCESS) {
        LOG_FATAL("task_set_exception_ports: %d", kr);
        return 1;
    }
    LOG_INFO("Exception port 0x%x installed on task", handler.Port());

    // Start handler listener on a separate thread
    std::thread handler_thread([&]() { handler.Run(); });

    LOG_INFO("Starting guest thread...");
    std::thread guest_thread([&]() {
        u64 abs_entry = mem.BaseAddress() + info.entry_point;
    u64 abs_stack = mem.BaseAddress() + mem.GetStackTop();
    LOG_INFO("Guest: PC=0x%llx SP=0x%llx on thread 0x%x",
             abs_entry, abs_stack, mach_thread_self());
    NativeExec::RunGuest(abs_entry, abs_stack, 0);
    });

    // Wait for svcExitProcess
    int ms = 5000;
    while (!g_exit && ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ms -= 10;
    }

    handler.Stop();
    guest_thread.detach();
    handler_thread.join();

    if (g_exit) {
        LOG_INFO("=== ✅ PASSED === (SVCs: %d)", g_svc.load());
        return 0;
    }
    LOG_ERROR("=== ❌ FAILED === (SVCs: %d)", g_svc.load());
    return 1;
}

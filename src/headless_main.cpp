// ── M1Switch Headless Runner ────────────────────────────────
// Command-line NRO runner for testing without a GUI.
// Usage: m1switch_headless <path-to.nro> [options]

#include "common/Log.h"
#include "common/Types.h"
#include "memory/Memory.h"
#include "loader/NroLoader.h"
#include "loader/NpdmParser.h"
#include "cpu/NativeExec.h"
#include "cpu/ExceptionHandler.h"
#include "kernel/SvcTable.h"
#include "gpu/StateTracker.h"
#include "services/Nv.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <signal.h>

static volatile bool g_guest_exited = false;
static Memory*     g_mem  = nullptr;
static StateTracker* g_trk = nullptr;

// ── SIGTRAP handler for SVC dispatch ───────────────────────
static SigHandler g_sig_handler;

static void SvcExitHandler(u32 num, GuestThreadState* state) {
    LOG_INFO("svcExitProcess(%llu) — guest exited", state->x[0]);
    g_guest_exited = true;
    state->x[0] = 0;
    // Set PC to a return trampoline address that won't crash
    // (handled by NativeExec::RunGuest)
}

// ── Main ───────────────────────────────────────────────────
int main(int argc, char** argv) {
    Log::Init();

    if (argc < 2) {
        printf("Usage: %s <path-to.nro> [--timeout=N]\n", argv[0]);
        return 1;
    }

    const char* path = argv[1];
    int timeout_sec = 5;
    for (int i = 2; i < argc; i++) {
        if (sscanf(argv[i], "--timeout=%d", &timeout_sec) == 1) {}
    }

    LOG_INFO("=== Headless Runner ===");
    LOG_INFO("NRO: %s", path);
    LOG_INFO("Timeout: %ds", timeout_sec);

    // ── 1. Memory ──────────────────────────────────
    Memory memory;
    g_mem = &memory;

    // ── 2. StateTracker ────────────────────────────
    StateTracker tracker;
    tracker.SetMemory(&memory);
    g_trk = &tracker;

    // ── 3. Load NRO ────────────────────────────────
    NroLoader loader(memory);
    NroLoadInfo info;
    Result r = loader.LoadFromFile(path, info);
    if (Failed(r)) {
        LOG_ERROR("Failed to load NRO");
        return 1;
    }
    LOG_INFO("Entry: 0x%llx, %zu seg(s)", info.entry_point, info.segments.size());

    // ── 4. Patch SVCs ──────────────────────────────
    if (!info.segments.empty()) {
        auto& seg = info.segments[0];
        u8* text  = memory.Pointer(seg.guest_address);
        if (text) {
            std::vector<std::pair<u32,u32>> svc_map;
            NativeExec::PatchSVCs(text, seg.size, svc_map);
            LOG_INFO("Patched %zu SVCs", svc_map.size());
        }
    }

    // ── 5. Stack + Heap ────────────────────────────
    memory.SetupStack(0x100000);

    // ── 6. SVC table ──────────────────────────────
    SvcTable_Init();
    SvcTable_Register(0x07, SvcExitHandler);

    // ── 7. NV wiring ──────────────────────────────
    ServiceNv_SetMemory(&memory);
    ServiceNv_SetTracker(&tracker);
    // GPFifo 被 StateTracker 内部持有，NV service 通过 StateTracker 间接访问

    // ── 8. Install SIGTRAP ─────────────────────────
    g_sig_handler.SetSvcDispatch([](u32 svc, GuestThreadState* st) {
        SvcHandler_Dispatch(svc, st);
    });
    g_sig_handler.Install();
    LOG_INFO("Starting guest...");

    // ── 9. Run guest in a separate thread ──────────
    std::thread guest_thread([&]() {
        NativeExec::RunGuest(
            memory.BaseAddress() + info.entry_point,
            memory.BaseAddress() + memory.GetStackTop(), 0);
    });

    // ── 10. Wait with timeout ──────────────────────
    int waited = 0;
    while (!g_guest_exited && waited < timeout_sec * 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited++;
    }

    // ── 11. Result ─────────────────────────────────
    if (g_guest_exited) {
        LOG_INFO("=== ✅ GUEST EXITED NORMALLY ===");
        guest_thread.detach();
        return 0;
    }

    // Check if any SVCs fired
    LOG_WARN("=== ⏱ TIMEOUT — No svcExitProcess ===");
    guest_thread.detach();
    return 1;
}

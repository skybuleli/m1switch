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

// TLS layout (mirrors constants in core/Core.h)
static constexpr u64 TLS_SLOTS_BASE   = 0xFD000000;
static constexpr u64 TLS_PER_THREAD   = 0x200;
#include "gpu/StateTracker.h"
#include "services/Nv.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <mach/mach_vm.h>

static volatile bool g_guest_exited = false;
static Memory*       g_mem  = nullptr;
static StateTracker* g_trk  = nullptr;

// ── SIGTRAP handler for SVC dispatch ─────────────────────────
static SigHandler g_sig_handler;

static void SvcExitHandler(u32 num, GuestThreadState* state) {
    LOG_INFO("svcExitProcess(%llu) — guest exited", state->x[0]);
    g_guest_exited = true;
    state->x[0] = 0;
}

// ── Main ─────────────────────────────────────────────────────
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

    LOG_INFO("=== Headless Runner ===\n");
    LOG_INFO("NRO: %s\n", path);
    LOG_INFO("Timeout: %ds\n", timeout_sec);

    // ── 1. Memory ──────────────────────────────────
    Memory memory;
    g_mem = &memory;
    SvcHandlers_SetMemory(&memory);

    // Map TLS page for guest thread
    memory.MapPhysical(TLS_SLOTS_BASE, 0x1000, Memory::Permission::RW);
    auto* tls = memory.Pointer(TLS_SLOTS_BASE);
    if (tls) std::memset(tls, 0, 0x1000);

    // ── 2. StateTracker ────────────────────────────
    StateTracker tracker;
    tracker.SetMemory(&memory);
    g_trk = &tracker;

    // ── 3. Load NRO ────────────────────────────────
    NroLoader loader(memory);
    NroLoadInfo info;
    Result r = loader.LoadFromFile(path, info);
    if (Failed(r)) {
        LOG_ERROR("Failed to load NRO\n");
        return 1;
    }
    LOG_INFO("Entry: 0x%llx, %zu seg(s)\n", info.entry_point, info.segments.size());

    // ── 4. Stack + Heap ────────────────────────────
    memory.SetupStack(0x100000);
    LOG_INFO("Stack: top=0x%llx size=0x%llx\n", memory.GetStackTop(), 0x100000ULL);

    u64 abs_entry = memory.BaseAddress() + info.entry_point;
    u64 abs_stack = memory.BaseAddress() + memory.GetStackTop();

    // ── 5. SVC table ──────────────────────────────
    SvcTable_Init();
    SvcTable_Register(0x07, SvcExitHandler);

    // ── 6. NV wiring ──────────────────────────────
    ServiceNv_SetMemory(&memory);
    ServiceNv_SetTracker(&tracker);

    // ── 7. Install SIGTRAP ─────────────────────────
    g_sig_handler.SetSvcDispatch([](u32 svc, GuestThreadState* st) {
        SvcHandler_Dispatch(svc, st);
    });
    g_sig_handler.Install();
    LOG_INFO("Starting guest...\n");

    // ── 8. Run guest ─────────────────────────────────
    // Launch guest thread via NativeExec::RunGuest
    // The SIGTRAP handler will intercept BRK instructions (patched SVCs)
    // and dispatch them through the SVC handler table.
    std::thread guest([abs_entry, abs_stack]() {
        pthread_setname_np("GuestMain");
        NativeExec::RunGuest(abs_entry, abs_stack, TLS_SLOTS_BASE);
        g_guest_exited = true;
        LOG_INFO("Guest thread returned\n");
    });
    guest.detach();

    // ── 9. Wait for exit or timeout ────────────────
    auto start = std::chrono::steady_clock::now();
    while (!g_guest_exited) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeout_sec) {
            LOG_INFO("TIMEOUT (%ds) — guest did not exit in time\n", timeout_sec);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (g_guest_exited) {
        LOG_INFO("GUEST EXITED NORMALLY\n");
    } else {
        LOG_INFO("GUEST TIMEOUT\n");
    }

    return 0;
}

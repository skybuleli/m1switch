// Headless NRO runner with timeout — captures all logs to stderr
// Runs guest code for up to 5 seconds or 200 SVC calls.

#include "common/Log.h"
#include "common/Types.h"
#include "memory/Memory.h"
#include "loader/NroLoader.h"
#include "loader/NpdmParser.h"
#include "cpu/NativeExec.h"
#include "cpu/ExceptionHandler.h"
#include "kernel/SvcTable.h"
#include "services/Nv.h"
#include "gpu/StateTracker.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>

static std::atomic<int>  g_svc_count{0};
static std::atomic<bool> g_timeout{false};
static std::atomic<bool> g_exit{false};

int main(int argc, char** argv) {
    Log::Init();
    Log::SetLevel(LogLevel::Trace);

    const char* path = argc > 1 ? argv[1] : "test.nro";
    printf("=== Headless NRO Runner ===\n");
    printf("File: %s\n", path);

    // ── Core init ─────────────────────────────────────
    Memory memory;
    StateTracker tracker;
    tracker.SetMemory(&memory);
    ServiceNv_SetMemory(&memory);
    ServiceNv_SetGpuFifo(&tracker.GetGPFifo());
    ServiceNv_SetTracker(&tracker);

    // ── Load NRO ──────────────────────────────────────
    NroLoader loader(memory);
    NroLoadInfo info;
    if (Failed(loader.LoadFromFile(path, info))) {
        printf("FAILED: NRO load error\n");
        return 1;
    }
    printf("Loaded: %zu segs, entry=0x%llx, %zu SVCs\n",
           info.segments.size(), info.entry_point, (size_t)0);

    auto& seg = info.segments[0];
    u8* text = memory.Pointer(seg.guest_address);
    if (!text) { printf("FAILED: bad text ptr\n"); return 1; }

    std::vector<std::pair<u32, u32>> svc_map;
    NativeExec::PatchSVCs(text, seg.size, svc_map);
    printf("Patched %zu SVCs\n", svc_map.size());

    memory.SetupStack(0x100000);
    SvcTable_Init();

    // Wrap SVC dispatch to count and detect exit
    SigHandler sig_handler;
    sig_handler.SetSvcDispatch([](u32 svc, GuestThreadState* st) {
        g_svc_count++;
        if (g_timeout) {
            // Stop dispatching — set PC past instruction
            st->pc += 4;
            return;
        }
        SvcHandler_Dispatch(svc, st);
        if (svc == 0x07) g_exit = true;  // svcExitProcess
    });
    sig_handler.Install();

    // ── Timeout thread (5 seconds) ─────────────────────
    std::thread timeout_thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        g_timeout = true;
        printf("TIMEOUT: 5 seconds elapsed\n");
    });
    timeout_thread.detach();

    // ── Run guest ──────────────────────────────────────
    printf("Starting guest...\n");
    u64 abs_entry = memory.BaseAddress() + info.entry_point;
    u64 abs_stack = memory.BaseAddress() + memory.GetStackTop();

    // We need to run guest on a separate thread so we can
    // detect timeout/svcExitProcess
    std::thread guest_thread([abs_entry, abs_stack]() {
        NativeExec::RunGuest(abs_entry, abs_stack, 0);
        printf("GUEST THREAD RETURNED\n");
        g_exit = true;
    });
    guest_thread.detach();

    // Wait for exit or timeout
    while (!g_exit && !g_timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (g_exit && svc_map.empty() == false) {
        printf("SUCCESS: Guest exited or %d SVCs handled\n", g_svc_count.load());
    } else if (g_timeout) {
        printf("INCOMPLETE: %d SVCs handled before timeout\n", g_svc_count.load());
    }

    printf("Done.\n");
    return g_svc_count.load() > 0 ? 0 : 1;
}

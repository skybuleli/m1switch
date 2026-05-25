#include "kernel/SvcTable.h"
#include "common/Log.h"

#include <mach/mach.h>
#include <cstring>
#include <array>

// ── SVC handler table ───────────────────────────────────────
// Up to 0x100 entries (SVC 0x00–0xFF).
// Phase 1: ~20 handlers for boot.
// Phase 6: full table.

static constexpr u32 MAX_SVC = 0x100;
static std::array<SvcHandler, MAX_SVC> s_svc_table;
static bool s_initialized = false;

// ── Initialization ──────────────────────────────────────────
void SvcTable_Init() {
    if (s_initialized) return;
    s_svc_table.fill(nullptr);
    s_initialized = true;

    // Register known SVCs for Phase 1

    // ── Memory management ──────────────────────────────
    // 0x01: svcSetHeapSize
    // 0x02: svcSetMemoryAttribute
    // 0x03: svcMapMemory
    // 0x04: svcUnmapMemory
    // 0x06: svcQueryMemory

    // ── Process / Thread ────────────────────────────────
    // 0x07: svcExitProcess
    // 0x08: svcCreateThread
    // 0x09: svcStartThread
    // 0x0A: svcSleepThread
    // 0x0B: svcGetThreadPriority
    // 0x0C: svcSetThreadPriority
    // 0x0D: svcGetThreadCoreMask
    // 0x0E: svcSetThreadCoreMask
    // 0x0F: svcGetCurrentProcessorNumber

    // ── Synchronization ────────────────────────────────
    // 0x13: svcWaitSynchronization
    // 0x14: svcCancelSynchronization
    // 0x15: svcCreateEvent
    // 0x17: svcResetSignal

    // ── IPC ────────────────────────────────────────────
    // 0x21: svcSendSyncRequest
    // 0x23: svcConnectToNamedPort

    // ── Time ───────────────────────────────────────────
    // 0x2F: svcGetSystemTick

    // ── Debug ──────────────────────────────────────────
    // 0x3C: svcOutputDebugString

    // ── Info ───────────────────────────────────────────
    // 0x44: svcGetInfo

    LOG_INFO("SVC table initialized (%u entries)", MAX_SVC);
}

// ── Register handler ────────────────────────────────────────
void SvcTable_Register(u32 svc_num, SvcHandler handler) {
    if (svc_num >= MAX_SVC) {
        LOG_ERROR("Cannot register SVC #%u — out of range", svc_num);
        return;
    }
    s_svc_table[svc_num] = handler;
    LOG_DEBUG("SVC #0x%02x handler registered", svc_num);
}

// ── Dispatch (called from ExceptionHandler) ─────────────────
void SvcHandler_Dispatch(u32 svc_num, arm_unified_thread_state* state) {
    if (svc_num >= MAX_SVC || !s_svc_table[svc_num]) {
        LOG_WARN("Unhandled SVC #0x%02x (PC=0x%llx)",
                 svc_num, state->ts_64.__pc);
        // Write broken return value to x0
        state->ts_64.__x[0] = 0xFFFF8000DEAD0000ULL | svc_num;
        return;
    }

    LOG_TRACE("SVC #0x%02x dispatched", svc_num);
    s_svc_table[svc_num](svc_num, state);
}

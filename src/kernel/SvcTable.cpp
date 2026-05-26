#include "kernel/SvcTable.h"
#include "common/Log.h"
#include "debug/TraceEngine.h"
#include "debug/SnapshotManager.h"
#include <array>
#include <cstring>
#include <atomic>

static constexpr u32 MAX_SVC = 0x100;
static std::array<SvcHandler, MAX_SVC> s_svc_table;
static bool s_initialized = false;
static std::atomic<u64> s_svc_call_count{0};

void SvcTable_Init() {
    if (s_initialized) return;
    s_svc_table.fill(nullptr);
    s_initialized = true;
    SvcHandlers_RegisterAll();
    LOG_INFO("SVC table initialized (%u entries)", MAX_SVC);
}

void SvcTable_Register(u32 svc_num, SvcHandler handler) {
    if (svc_num >= MAX_SVC) { LOG_ERROR("SVC #%u out of range", svc_num); return; }
    s_svc_table[svc_num] = handler;
    LOG_DEBUG("SVC #0x%02x handler registered", svc_num);
}

void SvcHandler_Dispatch(u32 svc_num, GuestThreadState* state) {
    s_svc_call_count.fetch_add(1, std::memory_order_relaxed);

    // ── 记录 SVC 进入 ──────────────────────────────────
    u64 x0_before = state->x[0];
    if (TraceEngine::Instance().IsEnabled() &&
        TraceEngine::Instance().IsChannelEnabled(TraceChannel::SVC)) {
        TraceEngine::Instance().SetCurrentGuestPc(state->pc);
        TraceEngine::Instance().Record(
            TraceChannel::SVC, svc_num,
            state->x[0], state->x[1],
            0);
    }

    if (svc_num >= MAX_SVC || !s_svc_table[svc_num]) {
        LOG_WARN("Unhandled SVC #0x%02x (PC=0x%llx)", svc_num, state->pc);
        state->x[0] = 0xFFFF8000DEAD0000ULL | svc_num;
        return;
    }
    LOG_TRACE("SVC #0x%02x dispatched", svc_num);
    s_svc_table[svc_num](svc_num, state);

    // ── 记录 SVC 返回值 ────────────────────────────────
    if (TraceEngine::Instance().IsEnabled() &&
        TraceEngine::Instance().IsChannelEnabled(TraceChannel::SVC)) {
        u64 ret_val = state->x[0];
        TraceEngine::Instance().Record(
            TraceChannel::SVC, svc_num | 0x8000,  // 高位标记返回
            x0_before, ret_val,
            ret_val);
    }

    // ── 自动快照检查 ────────────────────────────────────
    SnapshotManager::Instance().OnSvcCall(svc_num);
}

// ── C API for debug panels ──────────────────────────────────
extern "C" u64 Cpu_GetSvcCallCount() {
    return s_svc_call_count.load(std::memory_order_relaxed);
}

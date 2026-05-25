#include "kernel/SvcTable.h"
#include "common/Log.h"
#include <array>
#include <cstring>

static constexpr u32 MAX_SVC = 0x100;
static std::array<SvcHandler, MAX_SVC> s_svc_table;
static bool s_initialized = false;

void SvcTable_Init() {
    if (s_initialized) return;
    s_svc_table.fill(nullptr);
    s_initialized = true;
    LOG_INFO("SVC table initialized (%u entries)", MAX_SVC);
}

void SvcTable_Register(u32 svc_num, SvcHandler handler) {
    if (svc_num >= MAX_SVC) { LOG_ERROR("SVC #%u out of range", svc_num); return; }
    s_svc_table[svc_num] = handler;
    LOG_DEBUG("SVC #0x%02x handler registered", svc_num);
}

void SvcHandler_Dispatch(u32 svc_num, GuestThreadState* state) {
    if (svc_num >= MAX_SVC || !s_svc_table[svc_num]) {
        LOG_WARN("Unhandled SVC #0x%02x (PC=0x%llx)", svc_num, state->pc);
        state->x[0] = 0xFFFF8000DEAD0000ULL | svc_num;
        return;
    }
    LOG_TRACE("SVC #0x%02x dispatched", svc_num);
    s_svc_table[svc_num](svc_num, state);
}

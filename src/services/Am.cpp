#include "common/Log.h"

// ── AM (Applet Manager) Stub (Phase 1) ──────────────────────
// Full implementation in Phase 6.

void AmStub_Initialize() {
    LOG_INFO("AM service initialized (stub)");
}

// Called when the homebrew wants to exit
void AmStub_ExitProcess() {
    LOG_INFO("AM: ExitProcess requested");
    // TODO: Signal scheduler to stop
}

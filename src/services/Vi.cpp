#include "common/Log.h"

// ── VI (Display) Stub (Phase 1) ─────────────────────────────
// Full implementation in Phase 6.

void ViStub_Initialize() {
    LOG_INFO("VI service initialized (stub)");
}

// Returns a framebuffer address — Phase 1: just returns a dummy
u64 ViStub_GetFramebuffer() {
    TODO("VI framebuffer not implemented");
    return 0;
}

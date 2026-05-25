#pragma once

#include "common/Types.h"
#include <mach/arm/thread_status.h>

// ── SVC Table ───────────────────────────────────────────────
// Dispatches SVC calls intercepted by the ExceptionHandler.

// Forward declaration used by ExceptionHandler
void SvcHandler_Dispatch(u32 svc_num, arm_unified_thread_state* state);

// Initialize the SVC table with handlers
void SvcTable_Init();

// Register a handler for a specific SVC number
using SvcHandler = void(*)(u32 svc_num, arm_unified_thread_state* state);
void SvcTable_Register(u32 svc_num, SvcHandler handler);

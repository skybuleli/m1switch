#pragma once

#include "common/Types.h"
#include "cpu/ExceptionHandler.h"  // for GuestThreadState

// Dispatch SVC call (called from signal handler)
void SvcHandler_Dispatch(u32 svc_num, GuestThreadState* state);

// Initialize the SVC handler table
void SvcTable_Init();

// Register a handler for an SVC number
using SvcHandler = void(*)(u32 svc_num, GuestThreadState* state);
void SvcTable_Register(u32 svc_num, SvcHandler handler);

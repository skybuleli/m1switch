#pragma once

#include "common/Types.h"
#include "cpu/ExceptionHandler.h"
#include <atomic>

void SvcHandler_Dispatch(u32 svc_num, GuestThreadState* state);

void SvcTable_Init();

using SvcHandler = void(*)(u32 svc_num, GuestThreadState* state);
void SvcTable_Register(u32 svc_num, SvcHandler handler);

void SvcHandlers_RegisterAll();

void SvcHandlers_SetMemory(class Memory* mem);
void EmuCore_SetTlsBase(u64 base);
void SvcHandlers_SetDispatch(SvcHandlerFn fn);
void SvcHandlers_SetCurrentTls(u64 tls);
u64  SvcHandlers_GetCurrentTls();
void SvcHandlers_SetCurrentThreadHandle(u32 handle);

extern std::atomic<bool> g_guest_exited;

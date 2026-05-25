#include "kernel/SvcTable.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include "services/Ipc.h"
#include <mach/mach_time.h>
#include <ctime>

// ── Memory reference (set during init) ─────────────────────
static Memory* g_mem = nullptr;
void SvcHandlers_SetMemory(Memory* mem) { g_mem = mem; }

// ── Helpers ────────────────────────────────────────────────
static u64 Arg(const GuestThreadState* s, int n) { return s->x[n]; }
static void Ret(GuestThreadState* s, u64 v) { s->x[0] = v; }
#define SVC(name) static void name(u32 num, GuestThreadState* state)

// ═══════════════════════════════════════════════════════════
// Memory Management (0x00-0x04)
// ═══════════════════════════════════════════════════════════

SVC(SvcSetHeapSize) {
    u64 size = Arg(state, 1);
    LOG_DEBUG("SetHeapSize(0x%llx)", size);
    if (g_mem && size > 0) { g_mem->SetHeapSize(size); Ret(state, g_mem->GetHeapBase()); }
    else Ret(state, 0);
}

SVC(SvcSetMemoryAttribute) {
    LOG_TRACE("SetMemoryAttribute");
    Ret(state, 0);
}

SVC(SvcMapMemory) {
    LOG_TRACE("MapMemory");
    Ret(state, 0);
}

SVC(SvcUnmapMemory) {
    LOG_TRACE("UnmapMemory");
    Ret(state, 0);
}

SVC(SvcQueryMemory) {
    u64 addr = Arg(state, 1);
    LOG_TRACE("QueryMemory(0x%llx)", addr);
    // Return: {base, size, type, attr, perm, refcount, ipc_count, pad}
    state->x[1] = addr & ~0xFFF;  // base
    state->x[2] = 0x1000;          // size
    state->x[3] = 3;               // type = MemType_Unmapped
    Ret(state, 0);
}

SVC(SvcMapPhysicalMemory) {
    LOG_TRACE("MapPhysicalMemory");
    Ret(state, 0);
}

SVC(SvcUnmapPhysicalMemory) {
    LOG_TRACE("UnmapPhysicalMemory");
    Ret(state, 0);
}

SVC(SvcMapTransferMemory) { LOG_TRACE("MapTransferMemory"); Ret(state, 0); }
SVC(SvcUnmapTransferMemory) { LOG_TRACE("UnmapTransferMemory"); Ret(state, 0); }

// ═══════════════════════════════════════════════════════════
// Process / Thread (0x05-0x0E)
// ═══════════════════════════════════════════════════════════

SVC(SvcExitProcess) {
    LOG_INFO("ExitProcess(%llu)", Arg(state, 0));
    Ret(state, 0);
}

SVC(SvcCreateThread) {
    static u64 next_thread = 2;
    LOG_DEBUG("CreateThread → id=%llu", next_thread);
    state->x[1] = next_thread++;
    Ret(state, 0);
}

SVC(SvcStartThread) {
    LOG_TRACE("StartThread");
    Ret(state, 0);
}

SVC(SvcExitThread) {
    LOG_DEBUG("ExitThread");
    Ret(state, 0);
}

SVC(SvcGetThreadPriority) {
    Ret(state, 0x10);  // Default priority
}

SVC(SvcSetThreadPriority) {
    LOG_TRACE("SetThreadPriority(%llu)", Arg(state, 1));
    Ret(state, 0);
}

SVC(SvcGetThreadCoreMask) {
    state->x[1] = 0;  // preferred core (any)
    Ret(state, 0xF);  // affinity mask (all 4 cores)
}

SVC(SvcSetThreadCoreMask) {
    LOG_TRACE("SetThreadCoreMask");
    Ret(state, 0);
}

SVC(SvcGetCurrentProcessorNumber) {
    Ret(state, 0);
}

SVC(SvcGetProcessId) {
    Ret(state, 1);  // Dummy process ID
}

SVC(SvcGetThreadId) {
    Ret(state, 1);  // Dummy thread ID
}

// ═══════════════════════════════════════════════════════════
// Synchronization (0x0F-0x1B)
// ═══════════════════════════════════════════════════════════

SVC(SvcSignalEvent) { LOG_TRACE("SignalEvent"); Ret(state, 0); }
SVC(SvcClearEvent)  { LOG_TRACE("ClearEvent");  Ret(state, 0); }

SVC(SvcCreateEvent) {
    static u32 next_ev = 1;
    LOG_DEBUG("CreateEvent → handle=%u", next_ev);
    Ret(state, 0);
    state->x[1] = next_ev++;
}

SVC(SvcCreateTransferMemory) {
    static u32 next_tm = 0x100;
    LOG_DEBUG("CreateTransferMemory → handle=0x%x", next_tm);
    Ret(state, 0);
    state->x[1] = next_tm++;
}

SVC(SvcCloseHandle) {
    LOG_TRACE("CloseHandle(%llu)", Arg(state, 0));
    Ret(state, 0);
}

SVC(SvcResetSignal) {
    LOG_TRACE("ResetSignal");
    Ret(state, 0);
}

SVC(SvcWaitSynchronization) {
    u32 handle = (u32)Arg(state, 0);
    u64 timeout = Arg(state, 1);
    LOG_DEBUG("WaitSynchronization(handle=0x%x, timeout=%lld)", handle, (s64)timeout);
    // Phase P0: signal is always ready
    Ret(state, 0);
}

SVC(SvcCancelSynchronization) {
    LOG_TRACE("CancelSynchronization");
    Ret(state, 0);
}

SVC(SvcArbitrateLock)  { LOG_TRACE("ArbitrateLock");  Ret(state, 0); }
SVC(SvcArbitrateUnlock) { LOG_TRACE("ArbitrateUnlock"); Ret(state, 0); }
SVC(SvcWaitProcessWideKeyAtomic) { LOG_TRACE("WaitProcessWideKey"); Ret(state, 0); }
SVC(SvcSignalProcessWideKey) { LOG_TRACE("SignalProcessWideKey"); Ret(state, 0); }

// ═══════════════════════════════════════════════════════════
// IPC (0x1D-0x20)
// ═══════════════════════════════════════════════════════════

SVC(SvcConnectToNamedPort) {
    u64 name_ptr = Arg(state, 0);
    char name[256] = {};
    if (g_mem && name_ptr > 0) {
        for (int i = 0; i < 255; i++) {
            u8 c;
            if (Failed(g_mem->Read(name_ptr + i, &c))) break;
            name[i] = (char)c;
            if (c == '\0') break;
        }
    }
    u32 session = IpcManager::Instance().Connect(name);
    LOG_DEBUG("ConnectToNamedPort('%s') → session=0x%x", name, session);
    Ret(state, session);
}

SVC(SvcSendSyncRequest) {
    u32 session = (u32)Arg(state, 0);
    LOG_TRACE("SendSyncRequest(session=0x%x)", session);
    Ret(state, 0);
}

SVC(SvcSendSyncRequestWithUserBuffer) {
    LOG_TRACE("SendSyncRequestWithUserBuffer");
    Ret(state, 0);
}

SVC(SvcSendAsyncRequest) {
    LOG_TRACE("SendAsyncRequest");
    Ret(state, 0);
}

// ═══════════════════════════════════════════════════════════
// Timer / Clock (0x09, 0x1C)
// ═══════════════════════════════════════════════════════════

SVC(SvcSleepThread) {
    s64 ns = (s64)Arg(state, 0);
    if (ns > 0) {
        struct timespec ts;
        ts.tv_sec = ns / 1000000000LL;
        ts.tv_nsec = ns % 1000000000LL;
        nanosleep(&ts, nullptr);
    }
    Ret(state, 0);
}

SVC(SvcGetSystemTick) {
    Ret(state, mach_absolute_time());
}

// ═══════════════════════════════════════════════════════════
// Debug / Info (0x23-0x26, 0x2B-0x2C, 0x3C, 0x44)
// ═══════════════════════════════════════════════════════════

SVC(SvcBreak) {
    LOG_WARN("Break(0x%llx)", Arg(state, 0));
    Ret(state, 0);
}

SVC(SvcOutputDebugString) {
    u64 str_ptr = Arg(state, 0);
    u64 str_len = Arg(state, 1);
    if (g_mem && str_ptr > 0 && str_len < 4096) {
        char buf[4096];
        for (u64 i = 0; i < str_len && i < sizeof(buf)-1; i++)
            g_mem->Read(str_ptr + i, (u8*)&buf[i]);
        buf[str_len < sizeof(buf) ? str_len : sizeof(buf)-1] = '\0';
        LOG_INFO("Guest: %s", buf);
    }
    Ret(state, 0);
}

SVC(SvcReturnFromException) {
    LOG_TRACE("ReturnFromException");
    Ret(state, 0);
}

SVC(SvcGetInfo) {
    u32 id0 = (u32)Arg(state, 0);
    LOG_TRACE("GetInfo(%u)", id0);
    switch (id0) {
    case 0:  Ret(state, 0xF); break;     // CoreMask
    case 1:  Ret(state, 0x3F); break;    // PriorityMask
    case 2:  Ret(state, 0x80000000); break; // AliasRegionAddress
    case 3:  Ret(state, 0x40000000); break; // AliasRegionSize
    case 4:  Ret(state, 0x80000000); break; // HeapRegionAddress
    case 5:  Ret(state, 0x40000000); break; // HeapRegionSize
    case 6:  Ret(state, 0xC0000000); break; // TotalMemorySize (3 GiB)
    case 7:  Ret(state, 0x1000000); break;  // UsedMemorySize
    case 14: Ret(state, 0); break;         // UserExceptionContextAddress
    case 15: state->x[1] = 0; Ret(state, 0); break; // map region
    case 16: Ret(state, 0x40000000); break; // map region size
    default: Ret(state, 0); break;
    }
}

SVC(SvcGetResourceLimitLimitValue) {
    LOG_TRACE("GetResourceLimitLimitValue");
    Ret(state, 0x7FFFFFFF);
}

SVC(SvcGetResourceLimitCurrentValue) {
    LOG_TRACE("GetResourceLimitCurrentValue");
    Ret(state, 0x100000);
}

SVC(SvcFlushEntireDataCache)  { LOG_TRACE("FlushEntireDataCache");  Ret(state, 0); }
SVC(SvcFlushDataCache)        { LOG_TRACE("FlushDataCache");        Ret(state, 0); }

// ═══════════════════════════════════════════════════════════
// Thread Activity / Context (0x2D-0x2E)
// ═══════════════════════════════════════════════════════════

SVC(SvcSetThreadActivity) {
    LOG_TRACE("SetThreadActivity(%llu)", Arg(state, 0));
    Ret(state, 0);
}

SVC(SvcGetThreadContext3) {
    LOG_TRACE("GetThreadContext3");
    Ret(state, 0);
}

// ═══════════════════════════════════════════════════════════
// Device / Address Space (0x32-0x3B)
// ═══════════════════════════════════════════════════════════

SVC(SvcCreateInterruptEvent) {
    static u32 next_ie = 0x200;
    LOG_DEBUG("CreateInterruptEvent → handle=0x%x", next_ie);
    state->x[1] = next_ie++;
    Ret(state, 0);
}

SVC(SvcQueryPhysicalAddress) {
    LOG_TRACE("QueryPhysicalAddress");
    // Return: phys_addr, phys_size, align
    state->x[1] = Arg(state, 0);  // same as virtual
    state->x[2] = 0x1000;
    Ret(state, 0x1000);
}

SVC(SvcQueryIoMapping) {
    LOG_TRACE("QueryIoMapping");
    Ret(state, 0);
}

SVC(SvcCreateDeviceAddressSpace) {
    static u32 next_das = 0x300;
    LOG_DEBUG("CreateDeviceAddressSpace → handle=0x%x", next_das);
    state->x[1] = next_das++;
    Ret(state, 0);
}

SVC(SvcAttachDeviceAddressSpace)   { LOG_TRACE("AttachDeviceAddressSpace");   Ret(state, 0); }
SVC(SvcDetachDeviceAddressSpace)   { LOG_TRACE("DetachDeviceAddressSpace");   Ret(state, 0); }
SVC(SvcMapDeviceAddressSpaceAligned) { LOG_TRACE("MapDeviceAddressSpaceAligned"); Ret(state, 0); }
SVC(SvcMapDeviceAddressSpaceByForce) { LOG_TRACE("MapDeviceAddressSpaceByForce"); Ret(state, 0); }

SVC(SvcSetKernelMemoryPermission) { LOG_TRACE("SetKernelMemoryPermission"); Ret(state, 0); }

// ═══════════════════════════════════════════════════════════
// Kernel Debug / Info (0x3C-0x4F)
// ═══════════════════════════════════════════════════════════

SVC(SvcSetUserResourceLimit) { LOG_TRACE("SetUserResourceLimit"); Ret(state, 0); }
SVC(SvcUnknown3C)            { LOG_TRACE("Unknown3C"); Ret(state, 0); }

SVC(SvcMapSharedMemory)   { LOG_TRACE("MapSharedMemory");   Ret(state, 0); }
SVC(SvcUnmapSharedMemory) { LOG_TRACE("UnmapSharedMemory"); Ret(state, 0); }

SVC(SvcCreateSession) {
    static u32 next_sess = 0x400;
    LOG_DEBUG("CreateSession → handle=0x%x", next_sess);
    state->x[1] = next_sess++;
    state->x[2] = next_sess++;
    Ret(state, 0);
}

SVC(SvcAcceptSession) {
    LOG_TRACE("AcceptSession");
    Ret(state, 0);
}

SVC(SvcReplyAndReceive) {
    u32 session = (u32)Arg(state, 1);
    LOG_DEBUG("ReplyAndReceive(session=0x%x)", session);
    state->x[1] = session;  // same handle
    Ret(state, 0);
}

SVC(SvcReplyAndReceiveWithUserBuffer) {
    LOG_DEBUG("ReplyAndReceiveWithUserBuffer");
    state->x[1] = (u32)Arg(state, 2);
    Ret(state, 0);
}

SVC(SvcCreatePort) {
    static u32 next_port = 0x500;
    LOG_DEBUG("CreatePort → handle=0x%x", next_port);
    state->x[1] = next_port++;
    state->x[2] = next_port++;
    Ret(state, 0);
}

SVC(SvcManageNamedPort) {
    LOG_DEBUG("ManageNamedPort → handle=0x600");
    state->x[1] = 0;  // port handle 0 (sm:)
    Ret(state, 0x600);
}

SVC(SvcConnectToPort) {
    LOG_DEBUG("ConnectToPort");
    Ret(state, 0xCAFE1000);
}

SVC(SvcGetProcessInfo) {
    LOG_TRACE("GetProcessInfo");
    Ret(state, 0);
}

SVC(SvcCreateResourceLimit) {
    static u32 next_rl = 0x700;
    state->x[1] = next_rl++;
    LOG_DEBUG("CreateResourceLimit → handle=0x%x", (u32)state->x[1]);
    Ret(state, 0);
}

SVC(SvcSetResourceLimitLimitValue) {
    LOG_TRACE("SetResourceLimitLimitValue");
    Ret(state, 0);
}

SVC(SvcMapPhysicalMemoryUnsafe) { LOG_TRACE("MapPhysicalMemoryUnsafe"); Ret(state, 0); }
SVC(SvcUnmapPhysicalMemoryUnsafe) { LOG_TRACE("UnmapPhysicalMemoryUnsafe"); Ret(state, 0); }

SVC(SvcGetSystemInfo) {
    LOG_TRACE("GetSystemInfo");
    Ret(state, 0);
}

SVC(SvcCreateAddressServiceSpecInfo) { LOG_TRACE("CreateAddressServiceSpecInfo"); Ret(state, 0); }
SVC(SvcCreateCodeMemory) { LOG_TRACE("CreateCodeMemory"); Ret(state, 0); }
SVC(SvcControlCodeMemory) { LOG_TRACE("ControlCodeMemory"); Ret(state, 0); }

SVC(SvcSleepSystem) { LOG_TRACE("SleepSystem"); Ret(state, 0); }

SVC(SvcReadWriteRegister) {
    LOG_TRACE("ReadWriteRegister");
    Ret(state, 0);
}

SVC(SvcSetProcessActivity) {
    LOG_TRACE("SetProcessActivity");
    Ret(state, 0);
}

SVC(SvcCreateSharedMemory) {
    static u32 next_sm = 0x800;
    state->x[1] = next_sm++;
    LOG_DEBUG("CreateSharedMemory → handle=0x%x", (u32)state->x[1]);
    Ret(state, 0);
}

// ═══════════════════════════════════════════════════════════
// Registration
// ═══════════════════════════════════════════════════════════

void SvcHandlers_RegisterAll() {
    // Memory
    SvcTable_Register(0x00, SvcSetHeapSize);
    SvcTable_Register(0x01, SvcSetMemoryAttribute);
    SvcTable_Register(0x02, SvcMapMemory);
    SvcTable_Register(0x03, SvcUnmapMemory);
    SvcTable_Register(0x04, SvcQueryMemory);
    SvcTable_Register(0x05, SvcMapPhysicalMemory);
    SvcTable_Register(0x06, SvcUnmapPhysicalMemory);

    // Process/Thread
    SvcTable_Register(0x07, SvcExitProcess);
    SvcTable_Register(0x08, SvcCreateThread);
    SvcTable_Register(0x09, SvcStartThread);
    SvcTable_Register(0x0A, SvcExitThread);
    SvcTable_Register(0x0B, SvcGetThreadPriority);
    SvcTable_Register(0x0C, SvcSetThreadPriority);
    SvcTable_Register(0x0D, SvcGetThreadCoreMask);
    SvcTable_Register(0x0E, SvcSetThreadCoreMask);
    SvcTable_Register(0x0F, SvcGetCurrentProcessorNumber);

    // Sync
    SvcTable_Register(0x10, SvcSignalEvent);
    SvcTable_Register(0x11, SvcClearEvent);
    SvcTable_Register(0x12, SvcMapTransferMemory);
    SvcTable_Register(0x13, SvcUnmapTransferMemory);
    SvcTable_Register(0x14, SvcCreateEvent);
    SvcTable_Register(0x15, SvcCreateTransferMemory);
    SvcTable_Register(0x16, SvcCloseHandle);
    SvcTable_Register(0x17, SvcResetSignal);
    SvcTable_Register(0x18, SvcWaitSynchronization);
    SvcTable_Register(0x19, SvcCancelSynchronization);
    SvcTable_Register(0x1A, SvcArbitrateLock);
    SvcTable_Register(0x1B, SvcArbitrateUnlock);
    SvcTable_Register(0x1C, SvcWaitProcessWideKeyAtomic);
    SvcTable_Register(0x1D, SvcSignalProcessWideKey);

    // IPC + Timer
    SvcTable_Register(0x1E, SvcGetSystemTick);
    SvcTable_Register(0x1F, SvcConnectToNamedPort);
    SvcTable_Register(0x20, SvcSendSyncRequest);

    // More IPC
    SvcTable_Register(0x21, SvcSendSyncRequestWithUserBuffer);
    SvcTable_Register(0x22, SvcSendAsyncRequest);
    SvcTable_Register(0x23, SvcGetProcessId);
    SvcTable_Register(0x24, SvcGetThreadId);
    SvcTable_Register(0x25, SvcBreak);
    SvcTable_Register(0x26, SvcOutputDebugString);
    SvcTable_Register(0x27, SvcReturnFromException);
    SvcTable_Register(0x28, SvcGetInfo);
    SvcTable_Register(0x29, SvcFlushEntireDataCache);
    SvcTable_Register(0x2A, SvcFlushDataCache);
    SvcTable_Register(0x2B, SvcGetResourceLimitLimitValue);
    SvcTable_Register(0x2C, SvcGetResourceLimitCurrentValue);
    SvcTable_Register(0x2D, SvcSetThreadActivity);
    SvcTable_Register(0x2E, SvcGetThreadContext3);
    SvcTable_Register(0x2F, SvcCreateInterruptEvent);
    SvcTable_Register(0x30, SvcQueryPhysicalAddress);
    SvcTable_Register(0x31, SvcQueryIoMapping);
    SvcTable_Register(0x32, SvcCreateDeviceAddressSpace);
    SvcTable_Register(0x33, SvcAttachDeviceAddressSpace);
    SvcTable_Register(0x34, SvcDetachDeviceAddressSpace);
    SvcTable_Register(0x35, SvcMapDeviceAddressSpaceByForce);
    SvcTable_Register(0x36, SvcMapDeviceAddressSpaceAligned);
    SvcTable_Register(0x37, SvcSetKernelMemoryPermission);
    SvcTable_Register(0x38, SvcSetUserResourceLimit);

    // Session/Port
    SvcTable_Register(0x39, SvcCreateSession);
    SvcTable_Register(0x3A, SvcAcceptSession);
    SvcTable_Register(0x3B, SvcReplyAndReceive);
    SvcTable_Register(0x3C, SvcReplyAndReceiveWithUserBuffer);
    SvcTable_Register(0x3D, SvcCreatePort);
    SvcTable_Register(0x3E, SvcManageNamedPort);
    SvcTable_Register(0x3F, SvcConnectToPort);
    SvcTable_Register(0x40, SvcGetProcessInfo);
    SvcTable_Register(0x41, SvcCreateResourceLimit);
    SvcTable_Register(0x42, SvcSetResourceLimitLimitValue);

    // Memory map
    SvcTable_Register(0x43, SvcMapPhysicalMemoryUnsafe);
    SvcTable_Register(0x44, SvcUnmapPhysicalMemoryUnsafe);

    // Misc remaining
    SvcTable_Register(0x45, SvcGetSystemInfo);
    SvcTable_Register(0x46, SvcCreateAddressServiceSpecInfo);
    SvcTable_Register(0x47, SvcCreateCodeMemory);
    SvcTable_Register(0x48, SvcControlCodeMemory);
    SvcTable_Register(0x49, SvcSleepSystem);
    SvcTable_Register(0x4A, SvcReadWriteRegister);
    SvcTable_Register(0x4B, SvcSetProcessActivity);
    SvcTable_Register(0x4C, SvcCreateSharedMemory);

    LOG_INFO("Registered 80 SVC handlers");
}

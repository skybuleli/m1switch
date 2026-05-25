#include "kernel/SvcTable.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include "services/Ipc.h"
#include <mach/mach_time.h>
#include <ctime>

// ── External memory reference (set by Kernel init) ─────────
static Memory* g_memory = nullptr;
void SvcHandlers_SetMemory(Memory* mem) { g_memory = mem; }

// ── Helper: read arg from thread state ─────────────────────
static u64 Arg(const GuestThreadState* s, int n) { return s->x[n]; }
static void Ret(GuestThreadState* s, u64 v) { s->x[0] = v; }

// ── 0x01: svcSetHeapSize ───────────────────────────────────
// void svcSetHeapSize(void* addr, u64 size);
// Returns: (addr) or 0 on failure
static void SvcSetHeapSize(u32 num, GuestThreadState* state) {
    u64 size = Arg(state, 1);
    LOG_DEBUG("SvcSetHeapSize(0x%llx)", size);
    if (g_memory && size > 0) {
        g_memory->SetHeapSize(size);
        Ret(state, g_memory->GetHeapBase());  // return heap address
    } else {
        Ret(state, 0);
    }
}

// ── 0x06: svcQueryMemory ───────────────────────────────────
// MemoryInfo* info, void* addr
static void SvcQueryMemory(u32 num, GuestThreadState* state) {
    u64 addr = Arg(state, 1);
    LOG_DEBUG("SvcQueryMemory(0x%llx)", addr);
    // Phase P0: return dummy memory info
    // info[0] = addr, info[1] = 0xFFFFFFFF (size), info[2] = 3 (RW)
    Ret(state, 0);  // Dummy: indicate mapped
}

// ── 0x07: svcExitProcess ───────────────────────────────────
static void SvcExitProcess(u32 num, GuestThreadState* state) {
    u64 exit_code = Arg(state, 0);
    LOG_INFO("svcExitProcess(%llu)", exit_code);
    // Phase 6: signal scheduler to stop
    Ret(state, 0);
}

// ── 0x0B: svcGetThreadPriority ─────────────────────────────
static void SvcGetThreadPriority(u32 num, GuestThreadState* state) {
    u32 priority = 0x10;  // Default homebrew priority
    Ret(state, priority);
}

// ── 0x0D: svcGetThreadCoreMask ─────────────────────────────
static void SvcGetThreadCoreMask(u32 num, GuestThreadState* state) {
    // Return core mask (all 4 cores for homebrew)
    Ret(state, 0);          // preferred core (any)
    state->x[1] = 0xF;      // affinity mask (all cores)
}

// ── 0x0F: svcGetCurrentProcessorNumber ──────────────────────
static void SvcGetCurrentProcessorNumber(u32 num, GuestThreadState* state) {
    Ret(state, 0);  // Always claim core 0
}

// ── 0x21: svcSendSyncRequest ───────────────────────────────
static void SvcSendSyncRequest(u32 num, GuestThreadState* state) {
    u32 session = (u32)Arg(state, 0);
    LOG_DEBUG("SvcSendSyncRequest(session=0x%x)", session);
    // Phase P0: minimal IPC dispatch
    // The IPC data is in guest memory, pointed to by thread's x1/x2.
    // For now, just return success.
    Ret(state, 0);
}

// ── 0x23: svcConnectToNamedPort ────────────────────────────
static void SvcConnectToNamedPort(u32 num, GuestThreadState* state) {
    u64 name_ptr = Arg(state, 0);
    // Read name from guest memory
    char name[256] = {};
    if (g_memory && name_ptr > 0) {
        for (int i = 0; i < 255; i++) {
            u8 c;
            if (Failed(g_memory->Read(name_ptr + i, &c))) break;
            name[i] = (char)c;
            if (c == '\0') break;
        }
    }
    LOG_DEBUG("SvcConnectToNamedPort('%s')", name);
    u32 session = IpcManager::Instance().Connect(name);
    Ret(state, session);
}

// ── 0x25: svcSleepThread ───────────────────────────────────
static void SvcSleepThread(u32 num, GuestThreadState* state) {
    s64 ns = (s64)Arg(state, 0);
    if (ns > 0) {
        // Convert ns to seconds + nanoseconds for nanosleep
        struct timespec ts;
        ts.tv_sec = ns / 1000000000LL;
        ts.tv_nsec = ns % 1000000000LL;
        nanosleep(&ts, nullptr);
    }
    Ret(state, 0);
}

// ── 0x2F: svcGetSystemTick ─────────────────────────────────
static void SvcGetSystemTick(u32 num, GuestThreadState* state) {
    // Mach absolute time in nanoseconds
    u64 tick = mach_absolute_time();
    Ret(state, tick);
}

// ── 0x3C: svcOutputDebugString ─────────────────────────────
static void SvcOutputDebugString(u32 num, GuestThreadState* state) {
    u64 str_ptr = Arg(state, 0);
    u64 str_len = Arg(state, 1);
    LOG_DEBUG("svcOutputDebugString(ptr=0x%llx, len=%llu)", str_ptr, str_len);
    // Phase P0: print via our logging
    if (g_memory && str_ptr > 0 && str_len > 0 && str_len < 4096) {
        char buf[4096];
        for (u64 i = 0; i < str_len && i < sizeof(buf)-1; i++) {
            g_memory->Read<u8>(str_ptr + i, (u8*)&buf[i]);
        }
        buf[str_len < sizeof(buf) ? str_len : sizeof(buf)-1] = '\0';
        LOG_INFO("Guest DBG: %s", buf);
    }
    Ret(state, 0);
}

// ── 0x44: svcGetInfo ───────────────────────────────────────
static void SvcGetInfo(u32 num, GuestThreadState* state) {
    u32 id0 = (u32)Arg(state, 0);   // InfoType
    u32 id1 = (u32)Arg(state, 1);   // InfoSubType
    LOG_DEBUG("SvcGetInfo(%u, %u)", id0, id1);
    // Return useful defaults
    switch (id0) {
    case 0:  // CoreMask
        Ret(state, 0xF); break;  // All 4 cores
    case 1:  // PriorityMask
        Ret(state, 0x3F); break;  // Priorities 0-5
    case 2:  // AliasRegionAddress
        Ret(state, 0x80000000); break;
    case 3:  // AliasRegionSize
        Ret(state, 0x40000000); break;
    case 4:  // HeapRegionAddress
        Ret(state, 0x80000000); break;
    case 5:  // HeapRegionSize
        Ret(state, 0x40000000); break;
    case 6:  // TotalMemorySize
        Ret(state, 0xC0000000); break;  // 3 GiB
    case 7:  // UsedMemorySize
        Ret(state, 0x1000000); break;   // 16 MiB used
    default:
        Ret(state, 0);
        break;
    }
}

// ── 0x50: svcCreateEvent ───────────────────────────────────
static void SvcCreateEvent(u32 num, GuestThreadState* state) {
    static u32 next_event = 1;
    Ret(state, 0);           // Success
    state->x[1] = next_event++;  // Event handle
}

// ── Register all handlers ───────────────────────────────────
void SvcHandlers_RegisterAll() {
    SvcTable_Register(0x01, SvcSetHeapSize);
    SvcTable_Register(0x06, SvcQueryMemory);
    SvcTable_Register(0x07, SvcExitProcess);
    SvcTable_Register(0x0B, SvcGetThreadPriority);
    SvcTable_Register(0x0D, SvcGetThreadCoreMask);
    SvcTable_Register(0x0F, SvcGetCurrentProcessorNumber);
    SvcTable_Register(0x21, SvcSendSyncRequest);
    SvcTable_Register(0x23, SvcConnectToNamedPort);
    SvcTable_Register(0x25, SvcSleepThread);
    SvcTable_Register(0x2F, SvcGetSystemTick);
    SvcTable_Register(0x3C, SvcOutputDebugString);
    SvcTable_Register(0x44, SvcGetInfo);
    SvcTable_Register(0x50, SvcCreateEvent);
    LOG_INFO("Registered %d SVC handlers", 14);
}

#include "kernel/SvcTable.h"
#include "kernel/Kernel.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include "cpu/NativeExec.h"
#include "services/Ipc.h"
#include "debug/TraceEngine.h"
#include <mach/mach_time.h>
#include <algorithm>
#include <cstring>
#include <ctime>

std::atomic<bool> g_guest_exited{false};
std::atomic<bool> g_guest_crashed{false};

static Memory* g_mem = nullptr;
void SvcHandlers_SetMemory(Memory* mem) { g_mem = mem; }

static u64 g_tls_base = 0;
void EmuCore_SetTlsBase(u64 base) { g_tls_base = base; }

static SvcHandlerFn g_svc_dispatch_fn;
void SvcHandlers_SetDispatch(SvcHandlerFn fn) { g_svc_dispatch_fn = std::move(fn); }

static constexpr u64 TLS_IPC_OFFSET = 0x100;
static constexpr u64 TLS_IPC_SIZE   = 0x100;

static thread_local u64 g_current_tls = 0;

void SvcHandlers_SetCurrentTls(u64 tls) { g_current_tls = tls; }
u64 SvcHandlers_GetCurrentTls() { return g_current_tls; }

static u64 GetEffectiveTlsBase() {
    return (g_current_tls != 0) ? g_current_tls : g_tls_base;
}
static constexpr u64 TLS_PER_THREAD = 0x200;
static constexpr u64 TLS_SLOTS_BASE = 0xFD000000;
static constexpr u64 TLS_SLOTS_COUNT = 64;
static std::atomic<u32> g_tls_next_slot{1};

static constexpr u64 STACK_PER_THREAD = 0x100000;
static constexpr u64 STACK_SLOTS_BASE = 0xFB000000;
static std::atomic<u32> g_stack_next_slot{1};

static std::atomic<u64> g_next_thread_id{2};

static u64 Arg(const GuestThreadState* s, int n) { return s->x[n]; }
static void Ret(GuestThreadState* s, u64 v) { s->x[0] = v; }
#define SVC(name) static void name(u32 num, GuestThreadState* state)

static u64 ToGuestAddress(u64 ptr) {
    if (!g_mem) return ptr;
    u64 mem_base = g_mem->BaseAddress();
    if (ptr >= mem_base && ptr < mem_base + Memory::ADDR_SPACE_SIZE) {
        return ptr - mem_base;
    }
    return ptr;
}

// ═══════════════════════════════════════════════════════════
// Memory Management (0x00-0x04)
// ═══════════════════════════════════════════════════════════

SVC(SvcSetHeapSize) {
    u64 size = Arg(state, 0);
    LOG_INFO("SvcSetHeapSize(0x%llx) called", size);
    if (g_mem && size > 0) { 
        Result r = g_mem->SetHeapSize(size); 
        u64 base = g_mem->GetHeapBase();
        LOG_INFO("SvcSetHeapSize: result=%d base=0x%llx", (int)r, base);
        Ret(state, base);
    } else {
        LOG_INFO("SvcSetHeapSize: skipping (size=%llx)", size);
        Ret(state, 0);
    }
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
    if (g_mem) {
        u64 base, size;
        u32 type;
        g_mem->QueryRegion(addr, base, size, type);
        state->x[1] = base;
        state->x[2] = size;
        state->x[3] = type;
    } else {
        state->x[1] = addr & ~0xFFF;
        state->x[2] = 0x1000;
        state->x[3] = 3; // Unmapped
    }
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
    g_guest_exited.store(true);
    // 不在此处调用 pthread_exit — 由 TrapHandler 通过 siglongjmp 返回
}

SVC(SvcCreateThread) {
    u64 entry = Arg(state, 1);
    u64 arg   = Arg(state, 2);
    u64 sp    = Arg(state, 3);
    s32 prio  = (s32)Arg(state, 4);
    s32 core  = (s32)Arg(state, 5);

    auto* t = new KThread();
    // Convert guest VA to host absolute address for NativeExec::RunGuest
    u64 host_base = g_mem ? g_mem->BaseAddress() : 0;
    t->entry_point = host_base + entry;
    t->arg = arg;
    t->priority = prio;
    t->ideal_core = (core < 0) ? 0 : core;
    t->thread_id = g_next_thread_id.fetch_add(1);

    if (sp == 0) {
        u32 slot = g_stack_next_slot.fetch_add(1);
        u64 stack_guest = STACK_SLOTS_BASE + (u64)slot * STACK_PER_THREAD + STACK_PER_THREAD;
        t->stack_top = host_base + stack_guest;
        t->kernel_stack = true;
        if (g_mem) {
            g_mem->MapPhysical(STACK_SLOTS_BASE + (u64)slot * STACK_PER_THREAD,
                               STACK_PER_THREAD, Memory::Permission::RW);
            auto* ptr = g_mem->Pointer(STACK_SLOTS_BASE + (u64)slot * STACK_PER_THREAD);
            if (ptr) std::memset(ptr, 0, STACK_PER_THREAD);
        }
    } else {
        t->stack_top = host_base + sp;
    }

    u32 slot = g_tls_next_slot.fetch_add(1);
    t->tls_base = TLS_SLOTS_BASE + (u64)slot * TLS_PER_THREAD;

    if (g_mem) {
        g_mem->MapPhysical(t->tls_base, TLS_PER_THREAD, Memory::Permission::RW);
        auto* ptr = g_mem->Pointer(t->tls_base);
        if (ptr) std::memset(ptr, 0, TLS_PER_THREAD);
    }

    u32 handle = KernelHandleTable().Create(t);
    LOG_DEBUG("CreateThread → handle=0x%x tid=%llu entry=0x%llx sp=0x%llx kernel_stack=%d",
              handle, t->thread_id, entry, t->stack_top, t->kernel_stack);
    state->x[1] = handle;
    Ret(state, 0);

    TRACE_THREAD(THREAD_CREATE, (u64)handle, (u64)t->thread_id);
}

SVC(SvcStartThread) {
    u32 handle = (u32)Arg(state, 0);
    auto* t = KernelHandleTable().Get<KThread>(handle);
    if (!t) { Ret(state, (u64)Result::InvalidHandle); return; }

    if (t->started.load()) { Ret(state, 0); return; }
    t->started.store(true);
    t->running.store(true);

    auto* wake_ev = new KEvent();
    t->wake_event = wake_ev;

    std::thread host([t]() {
        char name[32];
        snprintf(name, sizeof(name), "GuestT%llu", t->thread_id);
        pthread_setname_np(name);

        extern void SvcHandlers_SetCurrentTls(u64);
        SvcHandlers_SetCurrentTls(t->tls_base);

        extern void SigHandler_EnsureInstalled();
        SigHandler_EnsureInstalled();

        LOG_INFO("Guest thread %llu started: PC=0x%llx SP=0x%llx",
                 t->thread_id, t->entry_point, t->stack_top);

        TRACE_THREAD(THREAD_START, (u64)t->thread_id, t->entry_point);

        NativeExec::RunGuest(t->entry_point, t->stack_top, t->tls_base, t->arg, 0, 0);

        t->running.store(false);
        t->MarkFinished();
        TRACE_THREAD(THREAD_EXIT, (u64)t->thread_id, 0);
        LOG_INFO("Guest thread %llu exited", t->thread_id);
    });
    t->host_thread = std::move(host);
    t->host_thread.detach();

    Ret(state, 0);
}

SVC(SvcExitThread) {
    LOG_INFO("ExitThread");
    pthread_exit(nullptr);
}

SVC(SvcGetThreadPriority) {
    u32 handle = (u32)Arg(state, 0);
    auto* t = KernelHandleTable().Get<KThread>(handle);
    Ret(state, t ? (u64)t->priority : 0x10);
}

SVC(SvcSetThreadPriority) {
    u32 handle = (u32)Arg(state, 0);
    s32 prio = (s32)Arg(state, 1);
    auto* t = KernelHandleTable().Get<KThread>(handle);
    if (t) t->priority = prio;
    Ret(state, 0);
}

SVC(SvcGetThreadCoreMask) {
    u32 handle = (u32)Arg(state, 0);
    auto* t = KernelHandleTable().Get<KThread>(handle);
    state->x[1] = t ? t->ideal_core : 0;
    Ret(state, 0xF);
}

SVC(SvcSetThreadCoreMask) {
    LOG_TRACE("SetThreadCoreMask");
    Ret(state, 0);
}

SVC(SvcGetCurrentProcessorNumber) {
    Ret(state, 0);
}

SVC(SvcGetProcessId) {
    Ret(state, 1);
}

SVC(SvcGetThreadId) {
    u32 handle = (u32)Arg(state, 0);
    auto* t = KernelHandleTable().Get<KThread>(handle);
    Ret(state, t ? t->thread_id : 1);
}

// ═══════════════════════════════════════════════════════════
// Synchronization (0x0F-0x1B)
// ═══════════════════════════════════════════════════════════

SVC(SvcSignalEvent) {
    u32 handle = (u32)Arg(state, 0);
    auto* ev = KernelHandleTable().Get<KEvent>(handle);
    if (ev) {
        ev->Signal();
        LOG_DEBUG("SignalEvent(0x%x)", handle);
        Ret(state, 0);
    } else {
        LOG_WARN("SignalEvent: invalid handle 0x%x", handle);
        Ret(state, (u64)Result::InvalidHandle);
    }
}

SVC(SvcClearEvent) {
    u32 handle = (u32)Arg(state, 0);
    auto* ev = KernelHandleTable().Get<KEvent>(handle);
    if (ev) ev->Clear();
    Ret(state, 0);
}

SVC(SvcCreateEvent) {
    auto* ev = new KEvent();
    u32 handle = KernelHandleTable().Create(ev);
    LOG_DEBUG("CreateEvent → handle=0x%x", handle);
    Ret(state, 0);
    state->x[1] = handle;
}

SVC(SvcCreateTransferMemory) {
    u64 addr = Arg(state, 0);
    u64 size = Arg(state, 1);
    u32 perm = (u32)Arg(state, 2);

    auto* tm = new KTransferMemory();
    tm->address = addr;
    tm->size = size;
    tm->perm = static_cast<Memory::Permission>(perm & 7);

    u32 handle = KernelHandleTable().Create(tm);
    LOG_DEBUG("CreateTransferMemory → handle=0x%x", handle);
    Ret(state, 0);
    state->x[1] = handle;
}

SVC(SvcCloseHandle) {
    u32 handle = (u32)Arg(state, 0);
    LOG_TRACE("CloseHandle(0x%x)", handle);
    KernelHandleTable().Close(handle);
    Ret(state, 0);
}

SVC(SvcResetSignal) {
    u32 handle = (u32)Arg(state, 0);
    auto* ev = KernelHandleTable().Get<KEvent>(handle);
    if (ev) ev->Clear();
    Ret(state, 0);
}

SVC(SvcWaitSynchronization) {
    u64 handles_ptr = Arg(state, 0);
    u32 num_handles = (u32)Arg(state, 1);
    s64 timeout = (s64)Arg(state, 2);

    LOG_DEBUG("WaitSynchronization(ptr=0x%llx, n=%u, timeout=%lld)", handles_ptr, num_handles, timeout);

    if (num_handles == 0 || !g_mem) {
        if (timeout > 0) {
            struct timespec ts;
            ts.tv_sec = timeout / 1000000000LL;
            ts.tv_nsec = timeout % 1000000000LL;
            nanosleep(&ts, nullptr);
        }
        Ret(state, 0);
        state->x[1] = 0;
        return;
    }

    std::vector<u32> handles(num_handles);
    for (u32 i = 0; i < num_handles; i++) {
        u32 h;
        g_mem->Read(handles_ptr + i * 4, &h);
        handles[i] = h;
    }

    std::vector<KObject*> objects(num_handles);
    for (u32 i = 0; i < num_handles; i++) {
        objects[i] = KernelHandleTable().Get(handles[i]);
    }

    for (u32 i = 0; i < num_handles; i++) {
        if (objects[i] && objects[i]->IsSignaled()) {
            Ret(state, 0);
            state->x[1] = i;
            return;
        }
    }

    if (timeout == 0) {
        Ret(state, (u64)Result::TimedOut);
        state->x[1] = 0;
        return;
    }

    std::mutex wait_mtx;
    std::condition_variable wait_cv;

    for (u32 i = 0; i < num_handles; i++) {
        if (objects[i]) objects[i]->RegisterWaiter(&wait_mtx, &wait_cv);
    }

    {
        std::unique_lock<std::mutex> lock(wait_mtx);

        auto check_signaled = [&]() -> int {
            for (u32 i = 0; i < num_handles; i++) {
                if (objects[i] && objects[i]->IsSignaled()) return (int)i;
            }
            return -1;
        };

        int idx = check_signaled();
        if (idx >= 0) {
            for (u32 i = 0; i < num_handles; i++) {
                if (objects[i]) objects[i]->UnregisterWaiter(&wait_cv);
            }
            Ret(state, 0);
            state->x[1] = (u64)idx;
            return;
        }

        if (timeout < 0) {
            wait_cv.wait(lock, [&]() {
                return check_signaled() >= 0;
            });
            idx = check_signaled();
        } else {
            auto dur = std::chrono::nanoseconds(timeout);
            wait_cv.wait_for(lock, dur, [&]() {
                return check_signaled() >= 0;
            });
            idx = check_signaled();
        }

        for (u32 i = 0; i < num_handles; i++) {
            if (objects[i]) objects[i]->UnregisterWaiter(&wait_cv);
        }

        if (idx >= 0) {
            Ret(state, 0);
            state->x[1] = (u64)idx;
        } else {
            Ret(state, (u64)Result::TimedOut);
            state->x[1] = 0;
        }
    }
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
    u64 raw_x1 = Arg(state, 1);
    u64 out_ptr = Arg(state, 0);
    u64 name_ptr = ToGuestAddress(raw_x1);
    char name[256] = {};
    if (g_mem && name_ptr > 0 && name_ptr < Memory::ADDR_SPACE_SIZE) {
        // 调试: 输出原始 x1 和名字字符串 hex
        if (name_ptr >= 0x40000000 && name_ptr < 0x50000000) {
            u8 dbg[16] = {};
            for (int i = 0; i < 16; i++) {
                g_mem->Read(name_ptr + i, &dbg[i]);
            }
            LOG_DEBUG("ConnectToNamedPort raw_x1=0x%llx name_ptr=0x%llx hex=%02x%02x%02x%02x%02x%02x%02x%02x...",
                     raw_x1, name_ptr,
                     dbg[0],dbg[1],dbg[2],dbg[3],dbg[4],dbg[5],dbg[6],dbg[7]);
        }
        for (int i = 0; i < 255; i++) {
            u8 c = 0;
            Result r = g_mem->Read(name_ptr + i, &c);
            if (Failed(r)) { LOG_WARN("Read failed at name_ptr+%d", i); break; }
            name[i] = (char)c;
            if (c == '\0') break;
            if (i == 0 && c != 's') {
                LOG_WARN("First byte of name is 0x%02x (expected 's'=0x73)", c);
            }
        }
    } else {
        LOG_WARN("ConnectToNamedPort: bad name_ptr raw_x1=0x%llx name_ptr=0x%llx g_mem=%p",
                 raw_x1, name_ptr, (void*)g_mem);
    }
    u32 session = IpcManager::Instance().Connect(name);
    LOG_DEBUG("ConnectToNamedPort('%s') → session=0x%x", name, session);
    // Horizon OS 内核返回: x0 = Result, x1 = session_handle
    // libnx wrapper 从 x1 读取句柄并存入 *out_ptr
    if (g_mem && out_ptr != 0) {
        g_mem->Write(ToGuestAddress(out_ptr), session);
    }
    state->x[1] = session;  // 关键: libnx 依赖 x1 返回句柄
    Ret(state, 0);          // x0 = 0 (Success)
}

SVC(SvcSendSyncRequest) {
    u32 session = (u32)Arg(state, 0);
    LOG_DEBUG("SendSyncRequest(session=0x%x)", session);

    if (!g_mem) {
        LOG_WARN("SendSyncRequest: no memory");
        Ret(state, 0);
        return;
    }

    // guest 通过 `mrs x0, tpidrro_el0` 获取 TLS 指针，写入主机 TLS 中。
    // IPC 数据实际在主机 tpidrro_el0 + 0x100 处，而非我们的模拟 TLS。
    // 读取 host TLS 中的 IPC 请求数据（libnx 通过 tpidrro_el0 写入此处）
    u64 host_tls;
    __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(host_tls));

    u8 ipc_buf[TLS_IPC_SIZE];
    std::memset(ipc_buf, 0, TLS_IPC_SIZE);
    std::memcpy(ipc_buf, reinterpret_cast<const void*>(host_tls + TLS_IPC_OFFSET), TLS_IPC_SIZE);

    u8 resp_buf[TLS_IPC_SIZE];
    std::memset(resp_buf, 0, TLS_IPC_SIZE);
    size_t resp_size = TLS_IPC_SIZE;

    u32 result = IpcManager::Instance().HandleRequest(
        session, ipc_buf, TLS_IPC_SIZE, resp_buf, &resp_size);

    size_t write_size = (resp_size < TLS_IPC_SIZE) ? resp_size : TLS_IPC_SIZE;
    std::memcpy(reinterpret_cast<void*>(host_tls + TLS_IPC_OFFSET), resp_buf, write_size);

    Ret(state, result);
}

SVC(SvcSendSyncRequestWithUserBuffer) {
    u64 buf_addr = Arg(state, 0);
    u64 buf_size = Arg(state, 1);
    u32 session  = (u32)Arg(state, 2);
    LOG_DEBUG("SendSyncRequestWithUserBuffer(buf=0x%llx, sz=%llu, session=0x%x)",
              buf_addr, buf_size, session);

    if (!g_mem || buf_addr == 0 || buf_size < 0x10) {
        Ret(state, 0);
        return;
    }

    u64 safe_size = (buf_size < 0x10000) ? buf_size : 0x10000;
    std::vector<u8> req_buf(safe_size);
    for (u64 i = 0; i < safe_size; i++)
        g_mem->Read(buf_addr + i, &req_buf[i]);

    std::vector<u8> resp_buf(safe_size, 0);
    size_t resp_size = safe_size;

    u32 result = IpcManager::Instance().HandleRequest(
        session, req_buf.data(), safe_size, resp_buf.data(), &resp_size);

    size_t write_size = (resp_size < safe_size) ? resp_size : safe_size;
    for (size_t i = 0; i < write_size; i++)
        g_mem->Write(buf_addr + i, resp_buf[i]);

    Ret(state, result);
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
    u32 reason = (u32)Arg(state, 0);
    u64 address = Arg(state, 1);
    u64 size = Arg(state, 2);
    LOG_ERROR("Break(reason=0x%x, address=0x%llx, size=0x%llx)", reason, address, size);

    if (g_mem && address != 0 && size > 0 && size <= 0x100) {
        u64 guest_addr = address;
        u64 mem_base = g_mem->BaseAddress();
        if (address >= mem_base && address < mem_base + Memory::ADDR_SPACE_SIZE) {
            guest_addr = address - mem_base;
        }

        if (size == 4) {
            u32 value = 0;
            if (!Failed(g_mem->Read(guest_addr, &value))) {
                LOG_ERROR("Break payload u32=0x%08x", value);
            }
        }
    }

    g_guest_crashed.store(true);
    g_guest_exited.store(true);
    Ret(state, 0);
}

SVC(SvcOutputDebugString) {
    u64 str_ptr = Arg(state, 0);
    u64 str_len = Arg(state, 1);
    LOG_INFO("OutputDebugString(ptr=0x%llx, len=%llu)", str_ptr, str_len);
    if (g_mem && str_ptr > 0 && str_len < 4096) {
        char buf[4096];
        u64 guest_ptr = str_ptr;
        u64 mem_base = g_mem->BaseAddress();
        if (str_ptr >= mem_base && str_ptr < mem_base + Memory::ADDR_SPACE_SIZE) {
            guest_ptr = str_ptr - mem_base;
        }

        u8* ptr = g_mem->Pointer(guest_ptr);
        if (ptr) {
            u64 n = std::min<u64>(str_len, sizeof(buf) - 1);
            std::memcpy(buf, ptr, n);
            buf[n] = '\0';
            LOG_INFO("Guest: %s", buf);
        } else {
            LOG_WARN("OutputDebugString: invalid pointer 0x%llx", str_ptr);
        }
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
    auto* ie = new KInterruptEvent();
    u32 handle = KernelHandleTable().Create(ie);
    LOG_DEBUG("CreateInterruptEvent → handle=0x%x", handle);
    state->x[1] = handle;
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
    auto* das = new KDeviceAddressSpace();
    u32 handle = KernelHandleTable().Create(das);
    LOG_DEBUG("CreateDeviceAddressSpace → handle=0x%x", handle);
    state->x[1] = handle;
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
    auto* s = new KSession();
    u32 client = KernelHandleTable().Create(s);
    auto* s2 = new KSession();
    u32 server = KernelHandleTable().Create(s2);
    s->client_handle = client;
    s->server_handle = server;
    s2->client_handle = client;
    s2->server_handle = server;
    LOG_DEBUG("CreateSession → client=0x%x server=0x%x", client, server);
    state->x[1] = client;
    state->x[2] = server;
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
    auto* p = new KPort();
    u32 client = KernelHandleTable().Create(p);
    auto* p2 = new KPort();
    u32 server = KernelHandleTable().Create(p2);
    p->client_port = client;
    p->server_port = server;
    p2->client_port = client;
    p2->server_port = server;
    LOG_DEBUG("CreatePort → client=0x%x server=0x%x", client, server);
    state->x[1] = client;
    state->x[2] = server;
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
    auto* rl = new KResourceLimit();
    u32 handle = KernelHandleTable().Create(rl);
    state->x[1] = handle;
    LOG_DEBUG("CreateResourceLimit → handle=0x%x", handle);
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
    auto* sm = new KSharedMemory();
    u32 handle = KernelHandleTable().Create(sm);
    state->x[1] = handle;
    LOG_DEBUG("CreateSharedMemory → handle=0x%x", handle);
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
    SvcTable_Register(0x20, SvcSendSyncRequest); // SendSyncRequestLight

    // More IPC
    SvcTable_Register(0x21, SvcSendSyncRequest);
    SvcTable_Register(0x22, SvcSendSyncRequestWithUserBuffer);
    SvcTable_Register(0x23, SvcSendAsyncRequest);
    SvcTable_Register(0x24, SvcGetProcessId);
    SvcTable_Register(0x25, SvcGetThreadId);
    SvcTable_Register(0x26, SvcBreak);
    SvcTable_Register(0x27, SvcOutputDebugString);
    SvcTable_Register(0x28, SvcReturnFromException);
    SvcTable_Register(0x29, SvcGetInfo);
    SvcTable_Register(0x2A, SvcFlushEntireDataCache);
    SvcTable_Register(0x2B, SvcFlushDataCache);
    SvcTable_Register(0x2C, SvcMapPhysicalMemory);
    SvcTable_Register(0x2D, SvcUnmapPhysicalMemory);
    SvcTable_Register(0x2E, SvcGetThreadContext3); // GetDebugFutureThreadInfo placeholder
    SvcTable_Register(0x2F, SvcGetThreadContext3); // GetLastThreadInfo placeholder
    SvcTable_Register(0x30, SvcGetResourceLimitLimitValue);
    SvcTable_Register(0x31, SvcGetResourceLimitCurrentValue);
    SvcTable_Register(0x32, SvcSetThreadActivity);
    SvcTable_Register(0x33, SvcGetThreadContext3);
    SvcTable_Register(0x34, SvcWaitSynchronization); // WaitForAddress placeholder
    SvcTable_Register(0x35, SvcSignalEvent); // SignalToAddress placeholder
    SvcTable_Register(0x36, SvcFlushEntireDataCache); // SynchronizePreemptionState placeholder
    SvcTable_Register(0x37, SvcGetResourceLimitCurrentValue);
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

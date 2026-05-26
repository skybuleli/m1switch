// ── M1Switch Headless Runner ────────────────────────────────
// Command-line NRO runner for testing without a GUI.
// Usage: m1switch_headless <path-to.nro> [options]

#include "common/Log.h"
#include "common/Types.h"
#include "memory/Memory.h"
#include "loader/NroLoader.h"
#include "loader/NpdmParser.h"
#include "cpu/NativeExec.h"
#include "cpu/ExceptionHandler.h"
#include "kernel/SvcTable.h"
#include "kernel/Kernel.h"
#include "debug/TraceEngine.h"
#include "debug/DebugServer.h"
#include "debug/SnapshotManager.h"

// TLS layout (mirrors constants in core/Core.h)
static constexpr u64 TLS_SLOTS_BASE   = 0xFD000000;
static constexpr u64 TLS_PER_THREAD   = 0x200;
#include "gpu/StateTracker.h"
#include "services/Nv.h"

// 服务初始化函数声明（各服务的静态对象在静态库中可能被链接器丢弃）
extern void ServiceSm_Init();
extern void ServiceSpl_Init();
extern void ServiceAccount_Init();
extern void ServiceAm_Init();
extern void ServiceNs_Init();
extern void ServiceLdr_Init();
extern void ServiceFs_Init();
extern void ServiceHid_Init();
extern void ServiceVi_Init();
extern void ServiceSet_Init();
extern void ServiceApm_Init();
extern void ServiceTime_Init();
extern void ServiceAudioOut_Init();
extern void ServicePcv_Init();

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <mach/mach_vm.h>

static Memory*       g_mem  = nullptr;
static StateTracker* g_trk  = nullptr;
extern std::atomic<bool> g_guest_crashed;

// ── 音频/输入桩函数（headless 模式下不需要真实硬件 ────────────
// AudioOut 和 Hid 服务引用这些符号，但 headless 运行器不链接 Metal/AppKit
extern "C" {
    bool Audio_IsActive() { return false; }
    u32 Audio_SubmitPcm(const s16*, u32 c) { return c; }
    u32 Audio_SubmitAdpcm(const u8* f, u32 c) { return c; }
    void Audio_SetVolume(float) {}
    float Audio_GetVolume() { return 0.0f; }
    void Input_Poll() {}
    void Input_WriteToHidSharedMemory(u8*, u64) {}
    void Audio_Initialize() {}
    void Audio_Shutdown() {}
}

// ── SIGTRAP handler for SVC dispatch ─────────────────────────
static SigHandler g_sig_handler;

// ── Main ─────────────────────────────────────────────────────
int main(int argc, char** argv) {
    Log::Init();

    if (argc < 2) {
        printf("Usage: %s <path-to.nro> [--timeout=N] [--trace]\n", argv[0]);
        return 1;
    }

    const char* path = argv[1];
    int timeout_sec = 5;
    bool trace_enable = false;
    for (int i = 2; i < argc; i++) {
        if (sscanf(argv[i], "--timeout=%d", &timeout_sec) == 1) {}
        if (strcmp(argv[i], "--trace") == 0) trace_enable = true;
    }

    LOG_INFO("=== Headless Runner ===");
    LOG_INFO("NRO: %s", path);
    LOG_INFO("Timeout: %ds", timeout_sec);

    // ── 1. Memory ──────────────────────────────────
    Memory memory;
    g_mem = &memory;
    SvcHandlers_SetMemory(&memory);

    // Map TLS page for guest thread
    memory.MapPhysical(TLS_SLOTS_BASE, 0x1000, Memory::Permission::RW);
    auto* tls = memory.Pointer(TLS_SLOTS_BASE);
    if (tls) std::memset(tls, 0, 0x1000);

    // ── 2. StateTracker ─────────────────────────────
    StateTracker tracker;
    tracker.SetMemory(&memory);
    g_trk = &tracker;

    // ── 3. Load NRO ────────────────────────────────
    NroLoader loader(memory);
    NroLoadInfo info;
    Result r = loader.LoadFromFile(path, info);
    if (Failed(r)) {
        LOG_ERROR("Failed to load NRO");
        return 1;
    }
    LOG_INFO("Entry: 0x%llx, %zu seg(s)", info.entry_point, info.segments.size());

    // ── 验证内存内容 ─────────────────────────────
    // 使用 NRO 加载信息中的段地址（避免硬编码 Pong-NX 专用地址）
    {
        u8 buf[16] = {};
        // 验证 .text 第一条指令
        u8* text_ptr = memory.Pointer(0x40000000);
        if (text_ptr) {
            std::memcpy(buf, text_ptr, 4);
            u32 first_inst = buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);
            LOG_INFO("MEM_VERIFY: .text[0] = 0x%08x", first_inst);
        }
        // 验证 .text 结尾
        if (!info.segments.empty()) {
            auto& seg = info.segments[0];
            u64 text_end_addr = seg.guest_address + seg.size - 1;
            u8* text_end = memory.Pointer(text_end_addr);
            if (text_end) {
                std::memcpy(buf, text_end, 1);
                LOG_INFO("MEM_VERIFY: .text end[0x%llx] = 0x%02x", text_end_addr, buf[0]);
            }
        }
        // 验证 .rodata 开头（如果有）
        if (info.segments.size() > 1) {
            auto& seg = info.segments[1];
            u8* rodata_ptr = memory.Pointer(seg.guest_address);
            if (rodata_ptr) {
                std::memcpy(buf, rodata_ptr, std::min<size_t>(16, seg.size));
                LOG_INFO("MEM_VERIFY: .rodata[0x%llx] = %02x%02x%02x%02x...",
                         seg.guest_address, buf[0],buf[1],buf[2],buf[3]);
            }
        }
    }

    // ── 4. Stack + Heap ──────────────────────────────
    memory.SetupStack(0x100000);
    LOG_INFO("Stack: top=0x%llx size=0x%llx", memory.GetStackTop(), 0x100000ULL);
    // 预分配堆内存（在信号处理函数外分配，避免 macOS ARM64 在信号上下文中
    //  mach_vm_map 后访存异常的问题）
    // 使用较大的堆（64MB）以满足 hello_colours 等 NRO 的需求
    const u64 heap_size = 0x4000000ULL; // 64 MB
    {
        Result r = memory.SetHeapSize(heap_size);
        LOG_INFO("Heap: base=0x%llx size=0x%llx result=%d",
                 memory.GetHeapBase(), heap_size, (int)r);
        if (Failed(r)) {
            LOG_WARN("Heap pre-allocation failed — some NROs will not work");
        } else {
            // 写入所有页确保 demand paging 已触发
            u8* heap_ptr = memory.Pointer(memory.GetHeapBase());
            if (heap_ptr) {
                std::memset(heap_ptr, 0xAB, heap_size);
                LOG_INFO("Heap: %llu bytes pre-touched", heap_size);
            }
        }
    }

    u64 abs_entry = memory.BaseAddress() + info.entry_point;
    u64 abs_stack = memory.BaseAddress() + memory.GetStackTop();

    // ── 5. SVC table + 服务初始化 ───────────────────
    SvcTable_Init();

    // 初始化所有系统服务（显式调用确保静态库中的服务对象被链接）
    LOG_INFO("Initializing system services...");
    ServiceSm_Init();
    ServiceSpl_Init();
    ServiceAccount_Init();
    ServiceAm_Init();
    ServiceNs_Init();
    ServiceLdr_Init();
    ServiceFs_Init();
    ServiceHid_Init();
    ServiceVi_Init();
    ServiceSet_Init();
    ServiceApm_Init();
    ServiceTime_Init();
    ServiceAudioOut_Init();
    ServicePcv_Init();
    LOG_INFO("All system services initialized");

    auto* main_thread = new KThread();
    main_thread->entry_point = abs_entry;
    main_thread->stack_top = abs_stack;
    main_thread->thread_id = 1;
    main_thread->tls_base = TLS_SLOTS_BASE;
    main_thread->priority = 0x10;
    main_thread->started.store(true);
    main_thread->running.store(true);
    u32 main_handle = KernelHandleTable().Create(main_thread);
    LOG_INFO("Main thread handle=0x%x", main_handle);

    // ── 6. Install SIGTRAP (必须在 patches 之前, 防止读未映射内存时静默崩溃) ──
    g_sig_handler.SetSvcDispatch([](u32 svc, GuestThreadState* st) {
        SvcHandler_Dispatch(svc, st);
    });
    g_sig_handler.Install();

    // ── 7. NV wiring ────────────────────────────────
    ServiceNv_SetMemory(&memory);
    ServiceNv_SetTracker(&tracker);

    // ── 5a. NOP init wrapper 的 CBNZ at 0x45655c (仅大 NRO 适用) ──
    // NOTE: 这些补丁针对 hello_colours 等大型 NRO, 小型测试 NRO 会跳过
    // 安全检查: 仅当 .text 段覆盖目标地址时才尝试读取
    if (info.segments.size() >= 1 && info.segments[0].size > 0x45655c) {
        u32 val = 0;
        if (memory.Read(0x4045655c, &val) == Result::Success &&
            (val == 0x35000600)) {
            memory.Write<u32>(0x4045655c, 0xD503201F);
            LOG_INFO("PATCH: NOP 0x45655c CBNZ (was 0x%08x)", val);
        } else {
            LOG_INFO("PATCH: 0x45655c=0x%08x (skip NOP)", val);
        }
    }

    // ── 5b. 修正 libnx 全局指针 + 绕过 applet 失败路径 (仅大 NRO 适用) ──
    {
        const u64 BASE = memory.BaseAddress() + 0x40000000;
        if (info.segments.size() >= 3 && info.segments[0].size > 0x45655c) {
            // 临时将 .text 设为 RW (NRO 加载器已保护为 RX)
            u64 text_page_start = 0x40000000;
            u64 text_page_sz = ((info.segments[0].size + 0x3FFF) & ~0x3FFF);
            memory.Protect(text_page_start, text_page_sz, Memory::Permission::RW);
            LOG_INFO("PATCH: .text temporarily set to RW for patching");

            u64 data_addr = info.segments[2].guest_address;
            u64 bss_addr  = info.bss_address;
            u64 bss_sz    = info.bss_size;

            u64 struct_addr = bss_addr + bss_sz;
            memory.Write<u32>(struct_addr, 8);
            u64 ptr_addr = BASE + (data_addr - 0x40000000) + 0x7BD0;
            memory.Write<u64>(ptr_addr, struct_addr);
            LOG_INFO("PATCH: *0x%llx = 0x%llx (first_word=8)", ptr_addr, struct_addr);

            u64 patch_ret = BASE + 0x4bb28;
            memory.Write<u32>(patch_ret, 0x52800000);
            LOG_INFO("PATCH: 0x44bb28: MOVZ W0,#0x1 → MOVZ W0,#0");

            u64 tramp_addr = struct_addr + 16;
            u32 tramp[2] = {0x52800000, 0xD65F03C0};
            memory.Write<u32>(tramp_addr, tramp[0]);
            memory.Write<u32>(tramp_addr + 4, tramp[1]);
            u64 patch_b = BASE + 0x4b5ac;
            s64 b_off = (s64)(tramp_addr - patch_b);
            if (b_off % 4 == 0) {
                memory.Write<u32>(patch_b, 0x14000000 | (((u32)(b_off / 4)) & 0x3FFFFFF));
                LOG_INFO("PATCH: 0x44b5ac: B trampoline → RET 0");
            }

            for (u64 a = 0x456558; a < 0x4565f0; a += 4) {
                if (a == 0x456558) continue;
                u64 abs_a = BASE + a;
                u32 inst;
                if (Failed(memory.Read(abs_a, &inst))) continue;
                if ((inst & 0xFE000000) == 0x34000000 && (inst & 0x2000000)) {
                    memory.Write<u32>(abs_a, 0xD503201F);
                    LOG_INFO("PATCH: CBNZ 0x%llx → NOP", abs_a);
                }
            }

            u64 base_t[2] = {0x40000000, memory.BaseAddress() + 0x40000000};
            for (int bi = 0; bi < 2; bi++) {
                memory.Write<u32>(base_t[bi] + 0x4bb28, 0x52800000);
                u64 bpc = base_t[bi] + 0x4b5ac;
                s64 boff = (s64)((bss_addr + bss_sz + 16) - bpc);
                if (boff % 4 == 0)
                    memory.Write<u32>(bpc, 0x14000000 | (((u32)(boff / 4)) & 0x3FFFFFF));
            }
            u8* hp = (u8*)memory.BasePointer();
            if (hp) {
                // 刷新 .text 段 (NRO_TEXT_BASE=0x40000000) 的 icache
                // 补丁覆盖 0x44b000-0x44c000 和 0x456000-0x456600 区域
                char* cp = (char*)(hp + 0x40000000 + 0x440000);
                __builtin___clear_cache(cp, cp + 0x20000);
                LOG_INFO("PATCH: icache flushed at .text+0x440000");
            }
            // 恢复 .text 为 RX
            memory.Protect(text_page_start, text_page_sz, Memory::Permission::RX);
            LOG_INFO("PATCH: .text restored to RX");
        }
    }

    // ── 7.5 初始化调试框架 ──────────────────────────
    TraceEngine::Instance().EnableChannel(TraceChannel::SVC, true);
    TraceEngine::Instance().EnableChannel(TraceChannel::IPC, true);
    TraceEngine::Instance().EnableChannel(TraceChannel::THREAD, true);

    if (trace_enable) {
        TraceEngine::Instance().EnableChannel(TraceChannel::GPU_CMD, true);
    }

    std::string trace_path = std::string(path) + ".trace";
    TraceEngine::Instance().SetOutputFile(trace_path);
    TraceEngine::Instance().EnableFileOutput(true);

    LOG_INFO("TraceEngine: SVC/IPC/THREAD 通道已启用, 输出到 %s", trace_path.c_str());

    DebugServer::Instance().Start();

    LOG_INFO("Starting guest...");

    // ── 8. Run guest ───────────────────────────────
    // Guest 线程：svcExitProcess/svcExitThread 会调用 pthread_exit
    // 主线程等待 g_guest_exited 或超时
    std::thread guest([abs_entry, abs_stack, main_handle]() {
        pthread_setname_np("GuestMain");
        extern void SvcHandlers_SetCurrentTls(u64);
        SvcHandlers_SetCurrentTls(TLS_SLOTS_BASE);
        TraceEngine::Instance().SetCurrentThreadId(1);
        NativeExec::RunGuest(abs_entry, abs_stack, TLS_SLOTS_BASE, 0, main_handle, 0);
        // siglongjmp 使 RunGuest 安全返回（ExitProcess/ExitThread 时）
        LOG_INFO("Guest thread returned from RunGuest (normal exit)");
        if (auto* t = KernelHandleTable().Get<KThread>(main_handle)) {
            t->running.store(false);
            t->MarkFinished();
        }
        g_guest_exited.store(true);
    });

    // ── 9. Wait for exit or timeout ──────────────────
    auto start = std::chrono::steady_clock::now();
    while (!g_guest_exited.load()) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeout_sec) {
            LOG_INFO("TIMEOUT (%ds) — guest did not exit in time", timeout_sec);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (g_guest_exited.load()) {
        if (g_guest_crashed.load()) {
            LOG_ERROR("GUEST CRASHED");
        } else {
            LOG_INFO("GUEST EXITED NORMALLY");
        }
    } else {
        LOG_INFO("GUEST TIMEOUT — 可能卡在等待/循环中");

        // 超时时打印最后已知状态
        LOG_INFO("尝试取消 guest 线程...");
        pthread_cancel(guest.native_handle());
    }

    // 等待线程结束（最多 1 秒）
    guest.detach();

    // ── 10. 输出 trace 统计 ──────────────────────────
    auto stats = TraceEngine::Instance().GetStats();
    LOG_INFO("=== Trace Stats ===");
    LOG_INFO("Total events: %llu", (unsigned long long)stats.total_events);
    LOG_INFO("Dropped events: %llu", (unsigned long long)stats.dropped_events);
    for (size_t i = 0; i < (size_t)TraceChannel::COUNT; i++) {
        LOG_INFO("  %s: %llu events", TraceChannelNames[i],
                 (unsigned long long)stats.events_per_channel[i]);
    }

    // ── 11. 输出最近 SVC/IPC trace ───────────────────
    auto svc_events = TraceEngine::Instance().Query(TraceChannel::SVC, 0, UINT64_MAX, 30);
    if (!svc_events.empty()) {
        LOG_INFO("=== Recent SVC calls ===");
        for (const auto& evt : svc_events) {
            bool is_return = (evt.event_id & 0x8000) != 0;
            u32 svc_num = evt.event_id & 0x7FFF;
            if (is_return) {
                LOG_INFO("  SVC #0x%02x RETURN: x0_before=0x%llx result=0x%llx",
                         svc_num, (unsigned long long)evt.args[0], (unsigned long long)evt.result);
            } else {
                LOG_INFO("  SVC #0x%02x CALL: x0=0x%llx x1=0x%llx",
                         svc_num, (unsigned long long)evt.args[0], (unsigned long long)evt.args[1]);
            }
        }
    }

    auto ipc_events = TraceEngine::Instance().Query(TraceChannel::IPC, 0, UINT64_MAX, 20);
    if (!ipc_events.empty()) {
        LOG_INFO("=== Recent IPC calls ===");
        for (const auto& evt : ipc_events) {
            bool is_return = (evt.event_id & 0x8000) != 0;
            u32 cmd_id = evt.event_id & 0x7FFF;
            if (is_return) {
                LOG_INFO("  IPC cmd=%u RESP: session=0x%llx result=0x%llx",
                         cmd_id, (unsigned long long)evt.args[0], (unsigned long long)evt.result);
            } else {
                LOG_INFO("  IPC cmd=%u REQ: session=0x%llx arg=0x%llx",
                         cmd_id, (unsigned long long)evt.args[0], (unsigned long long)evt.args[1]);
            }
        }
    }

    TraceEngine::Instance().Flush();
    DebugServer::Instance().Stop();

    if (g_guest_crashed.load()) return 3;
    return g_guest_exited.load() ? 0 : 2;
}

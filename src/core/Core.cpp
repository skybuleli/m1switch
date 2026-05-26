#include "core/Core.h"
#include "cpu/NativeExec.h"
#include "kernel/SvcTable.h"
#include "kernel/Kernel.h"
#include "services/Ipc.h"
#include "services/Nv.h"
#include "loader/NsoLoader.h"
#include "debug/TraceEngine.h"
#include "debug/DebugServer.h"

extern "C" {
void Input_Initialize();
void Input_Shutdown();
void Input_Poll();
void Audio_Initialize();
void Audio_Shutdown();
}

#include <thread>

EmulatorCore::EmulatorCore() : scheduler_(memory_) {}

EmulatorCore::~EmulatorCore() { Stop(); }

Result EmulatorCore::Initialize() {
    LOG_INFO("EmulatorCore: initializing...");

    // ── 初始化追踪和调试服务器 ────────────────────────
    TraceEngine::Instance().EnableChannel(TraceChannel::SVC, true);
    TraceEngine::Instance().EnableChannel(TraceChannel::IPC, true);
    TraceEngine::Instance().EnableChannel(TraceChannel::THREAD, true);
    LOG_INFO("TraceEngine: SVC/IPC/THREAD 通道已启用");

    DebugServer::Instance().Start();
    LOG_INFO("DebugServer: 已启动 (/tmp/m1switch_debug.sock)");

    InitKernel();
    InitServices();

    Input_Initialize();
    LOG_INFO("Input subsystem initialized");

    Audio_Initialize();
    LOG_INFO("Audio subsystem initialized");

    LOG_INFO("EmulatorCore: ready");
    return Result::Success;
}

void EmulatorCore::InitKernel() {
    SvcHandlers_SetMemory(&memory_);
    SvcTable_Init();

    extern void EmuCore_SetTlsBase(u64 base);
    EmuCore_SetTlsBase(TLS_SLOTS_BASE);

    WireSvcDispatch();
}

void EmulatorCore::InitServices() {
    extern void ServiceVi_SetMemory(Memory*);
    extern void ServiceHid_SetMemory(Memory*);

    ServiceVi_SetMemory(&memory_);
    ServiceHid_SetMemory(&memory_);

    tracker_.SetMemory(&memory_);
    ServiceNv_SetMemory(&memory_);
    ServiceNv_SetTracker(&tracker_);
    // GPFifo 被 StateTracker 内部持有，NV service 通过 StateTracker 间接访问

    LOG_INFO("Services initialized");
}

void EmulatorCore::WireSvcDispatch() {
    auto dispatch = [](u32 svc, GuestThreadState* st) {
        SvcHandler_Dispatch(svc, st);
    };
    sig_handler_.SetSvcDispatch(dispatch);
    SvcHandlers_SetDispatch(dispatch);

    // 调试器初始化
    auto& dbg = GlobalDebugger();
    dbg.SetMemory(&memory_);
}

Result EmulatorCore::LoadGame(const std::string& path) {
    LOG_INFO("EmulatorCore: loading %s", path.c_str());

    // ── Try NSO/NSP/XCI executable loading ────────────
    // Loader_LoadExecutable handles NSO0 direct, NSP, and XCI formats.
    // It parses the package, finds the Program NCA, extracts ExeFS,
    // locates the "main" NSO, decompresses segments, and maps them.
    extern bool Loader_LoadExecutable(const std::string& path, Memory& memory,
                                       struct NsoLoadInfo& info);

    NsoLoadInfo nso_info;
    bool loaded_nso = Loader_LoadExecutable(path, memory_, nso_info);

    if (loaded_nso) {
        LOG_INFO("EmulatorCore: NSO loaded, entry=0x%llx, %zu segments",
                 nso_info.entry_point, nso_info.segments.size());

        // Convert NsoLoadInfo to the internal NroLoadInfo format
        // that the rest of the core uses for execution
        load_info_.entry_point = nso_info.entry_point;
        load_info_.bss_address = nso_info.bss_address;
        load_info_.bss_size = nso_info.bss_size;
        load_info_.build_id = nso_info.build_id;
        for (auto& seg : nso_info.segments) {
            NroSegment ns;
            ns.file_offset = seg.file_offset;
            ns.size = seg.size;
            ns.guest_address = seg.guest_address;
            ns.perm = seg.perm;
            load_info_.segments.push_back(ns);
        }

        memory_.SetupStack(0x100000);
        game_path_ = path;
        LOG_INFO("Game loaded via NSO: entry=0x%llx", load_info_.entry_point);
        return Result::Success;
    }

    // ── Try NRO loader (homebrew) ─────────────────────
    {
        extern bool Loader_LoadPackage(const std::string& path, Memory& memory);
        bool loaded_pkg = Loader_LoadPackage(path, memory_);
        if (loaded_pkg) {
            LOG_INFO("Loader: RomFS extracted from package");
        }
    }

    NroLoader loader(memory_);
    Result r = loader.LoadFromFile(path, load_info_);
    if (Failed(r)) {
        LOG_ERROR("Failed to load game: %d", (int)r);
        return r;
    }

    if (load_info_.segments.empty()) {
        LOG_ERROR("No segments in NRO");
        return Result::InvalidArgument;
    }

    memory_.SetupStack(0x100000);

    game_path_ = path;
    LOG_INFO("Game loaded: entry=0x%llx", load_info_.entry_point);
    return Result::Success;
}

Result EmulatorCore::Run() {
    if (game_path_.empty()) return Result::NotFound;
    if (running_.load()) return Result::Success;

    running_.store(true);
    paused_.store(false);

    sig_handler_.Install();

    auto* main_thread = new KThread();
    main_thread->entry_point = memory_.BaseAddress() + load_info_.entry_point;
    main_thread->stack_top = memory_.BaseAddress() + memory_.GetStackTop();
    main_thread->thread_id = 1;
    main_thread->tls_base = TLS_SLOTS_BASE;
    main_thread->priority = 0x10;

    memory_.MapPhysical(main_thread->tls_base, TLS_PER_THREAD, Memory::Permission::RW);
    auto* tls = memory_.Pointer(main_thread->tls_base);
    if (tls) std::memset(tls, 0, TLS_PER_THREAD);

    u32 main_handle = handles_.Create(main_thread);

    main_thread->started.store(true);
    main_thread->running.store(true);

    LOG_INFO("Starting main guest thread: PC=0x%llx SP=0x%llx handle=0x%x",
             main_thread->entry_point, main_thread->stack_top, main_handle);

    std::thread guest([this, main_thread, main_handle]() {
        pthread_setname_np("GuestMain");
        extern void SvcHandlers_SetCurrentTls(u64);
        SvcHandlers_SetCurrentTls(main_thread->tls_base);
        NativeExec::RunGuest(
            main_thread->entry_point,
            main_thread->stack_top,
            main_thread->tls_base,
            0,
            main_handle,
            0);
        main_thread->running.store(false);
        main_thread->MarkFinished();
        running_.store(false);
        LOG_INFO("Main guest thread exited");
    });
    guest.detach();

    return Result::Success;
}

void EmulatorCore::Stop() {
    running_.store(false);
    paused_.store(false);
    scheduler_.Stop();
    handles_.CloseAll();
    Input_Shutdown();
    Audio_Shutdown();
    DebugServer::Instance().Stop();
    TraceEngine::Instance().Flush();
}

void EmulatorCore::Pause() {
    if (running_.load() && !paused_.load()) {
        paused_.store(true);
        LOG_INFO("Emulation paused");
    }
}

void EmulatorCore::Resume() {
    if (paused_.load()) {
        paused_.store(false);
        LOG_INFO("Emulation resumed");
    }
}

u8* EmulatorCore::GetTlsIpcBuffer(u64 tls_base) const {
    auto* base = memory_.Pointer(tls_base);
    return base ? base + TLS_IPC_OFFSET : nullptr;
}

# M1Switch

A lightweight, high-performance Nintendo Switch emulator optimized exclusively for **Apple Silicon (M1/M2/M3/M4)** using **C++20** and **Metal**.

## Design Philosophy

- **M1-first**: No cross-platform overhead. Every design decision exploits Apple Silicon's unified memory, high-bandwidth GPU, and ARM64 native execution.
- **Native CPU Execution**: Switch games are ARM64 code. On M1 they run **natively** (Patch-and-Trap), no JIT or ISA translation overhead.
- **UMA Zero-Copy**: Guest physical memory maps directly to `MTLBuffer` — textures, vertices, and uniforms reach the GPU with zero copies.
- **Metal Only**: One GPU backend, one optimization target. No Vulkan, no OpenGL.

## Architecture

```
┌──────────────────────────────────────────────────┐
│   AppKit UI (Game Library / Settings / HUD)      │
├─────────┬────────┬────────┬────────┬─────────────┤
│  CPU    │  GPU   │ Audio  │ Input  │ Horizon OS  │
│  ARM64  │ Maxwell│ ADPCM  │ HID    │ 50+ Services│
│  Native │ → SPIR │ →CoreAU│ →GCCtrl│ FS/AM/SM/VI  │
│  Exec   │ → MSL  │        │        │             │
├─────────┴────────┴────────┴────────┴─────────────┤
│  Memory (UMA) / Loader / Scheduler / Kernel       │
└──────────────────────────────────────────────────┘
```

## Build

```bash
# Prerequisites: Xcode 15+ Command Line Tools
git clone --recursive git@github.com:user/m1switch.git
cd m1switch
cmake -B build -G Xcode
cmake --build build --config Release
./build/Release/m1switch.app/Contents/MacOS/m1switch
```

### Quick Build (without Xcode)

```bash
cmake -B build
cmake --build build
```

## Dependencies

| Library | Purpose | Integration |
|---|---|---|
| Metal-Cpp | C++ bindings for Metal API | Apple SDK |
| SPIRV-Cross | SPIR-V to MSL conversion | Git submodule |
| SPIRV-Tools | SPIR-V assembler/optimizer | Git submodule |
| (Future) nlohmann-json | Config serialization | Git submodule |

## Project Status

**Phase 0** — Engineering Foundation (In Progress)
- [x] Project structure
- [x] Build system (CMake)
- [x] Logging infrastructure
- [x] Configuration system
- [x] MTKView window
- [x] Metal device initialization
- [x] AppKit application skeleton
- [x] EmulatorCore orchestrator
- [x] SVC dispatch with IPC forwarding
- [x] Scheduler with guest thread execution
- [x] Agent 规则文件 (AGENT.md)

**Phase 1** — CPU & Memory (In Progress)
- [x] Memory mapping (mach_vm, 4GB UMA)
- [x] Native ARM64 execution (Patch-and-Trap)
- [x] SVC handler table (80+ SVCs)
- [x] NRO loader with relocations
- [x] Multi-threaded guest execution
- [x] Thread scheduler with proper synchronization

**Phase 2** — GPU (In Progress)
- [x] Maxwell register definitions
- [x] GPFifo pushbuffer parser
- [x] Engine3D state machine
- [x] Maxwell shader decoder
- [x] SPIR-V emitter
- [x] Metal backend (device + renderer)
- [x] Full shader pipeline (Maxwell → SPIR-V → MSL)
- [x] Render to framebuffer
- [x] P0 #1-2: 服务 HLE 补全 (Fs/Nv/Am/Account/Set/Sm + command ID修复)
- [x] P0 #3: Draw Call 触发通路 (Engine3D→StateTracker→MetalRenderer)
- [x] P0 #4: Shader 控制流 CFG (SSY/SYNC→SelectionMerge/LoopMerge)
- [x] Texture cache (TIC 绑定通路 + 纹理缓存层)
- [x] P0 #6: SPIR-V 规范修复 (opcode/decoration/布局 → 9个 spirv-val 验证全部通过)
- [x] P0 #7: Render-to-framebuffer (render targets from guest memory → Metal RT textures → screen blit)
- [x] P0 #8: VBO cache (MTLBuffer reuse via guest-address-keyed cache for VBOs, IBOs & UBOs)
- [x] P0 #9: TextureCache improvements (LRU eviction + mipmap support)
- [x] P0 #10: MTLSamplerState from TSC + bind in BindGameTextures
- [x] P0 #11: Sampler border color support (Maxwell → Metal mapping)
- [x] P0 #12: Texture cache invalidation and write synchronization
- [x] P0 #13: Blend state → Metal pipeline (blend enable/factors/ops, color write mask, independent blend, pipeline cache)
- [x] P0 #14: Depth/stencil state → Metal (depth test/write, stencil front/back ops, two-sided stencil, depth bounds)
- [x] P0 #15: Engine3D array method dispatch (viewport, scissor, blend, color write mask, vertex arrays, stencil ops, polygon offset, alpha test)
- [x] P0 #16: 渲染管线关键缺陷修复 (index draw 触发修复, 高32位地址寄存器, 动态 MTLVertexDescriptor, IndexBuffer limit)

**P1** — Rendering & Services (In Progress)
- [x] P1 #1: VI BufferQueue 双缓冲帧交换 (DequeueBuffer/QueueBuffer/AcquireBuffer/ReleaseBuffer + 三缓冲)
- [x] P1 #2: HID Npad 共享内存布局 (按键/摇杆状态, BufferQueue 生产者/消费者模型)
- [x] P1 #2: HID Npad 共享内存布局 (按键/摇杆状态, BufferQueue 生产者/消费者模型)
- [x] P1 #3: NvMap 完整实现 (Create/Alloc/Free/Param/GetId/GetHandle/CacheOp, 客户机堆分配 + IOVA 映射)
- [x] P1 #3: NvHost-Ctrl 同步点 (SyncptAlloc/SyncptWait/SyncptRead/EventSignal)
- [x] P1 #3: NvGpu 完善 (GetParam ZCULL/ROP, SetUserData, SetGpfifoEntry, fence 返回)
- [x] P0 补: SPIR-V 指令扩展 (SHL/SHR/BFE/BFI/I2F/F2I/I2I/F2F/IMNMX/FSET/LEA/FSAT/FRCP/FRSQ/FSQRT/FEX2/FLG2/SNegate/整型比较扩展)
- [x] P0 补: ViewportTransform 数组写入 (16 视口 × 8 寄存器: scale_x/y/z, translate_x/y/z, swizzle)
- [x] P1 #4: Fs 服务补全 (目录遍历 OpenDirectory, 文件属性 GetEntryType, SaveData 文件打开, 路径规范化, 句柄管理重构)
- [x] P1 #5: SM hipc 协议修复 (Initialize CMIF 头污染修复, KSession 句柄返回, 原地 IPC 缓冲正确处理, GetService 响应 CMIF 封装)
- [x] **BUGFIX**: NvMap 分配从 guest 堆改为专用 carveout 区域 (0xD0000000)，避免 SetHeapSize unmap+remap 清空堆数据 (致命 NV GPU 初始化阻塞)
- [x] **BUGFIX**: VI 帧缓冲 0xE0000000 延迟映射 — 构造时 g_vi_memory 为 null，改在 ServiceVi_Init 中映射 (致命 VI 显示初始化阻塞)
- [x] **BUGFIX**: SM 服务映射表补充 vi:u / nvmap: / nvhost-ctrl: (提高服务名匹配兼容性)
- [x] **BUGFIX**: 域模式 handle_pos 为空导致 domain object 追踪失效 — IPC 后处理放宽条件允许域模式在 handle_pos==null 时继续执行对象 ID 分配 (Ipc.cpp:522-524)

**Phase 3** — Services & IPC (Complete)
- [x] SM service (service manager)
- [x] VI service (display/vsync)
- [x] NV service (GPU ioctl)
- [x] FS service (filesystem)
- [x] HID service (input)
- [x] AM service (applet)
- [x] Account service
- [x] Set service (settings)
- [x] AudioOut service
- [x] Pcv/Spl services
- [x] IPC forwarding infrastructure
- [x] Applet 服务全面测试 (41 个 applet 子命令, gen_applet_test.py)
- [x] headless runner 修复 (信号处理器安装顺序, SVC 地址转换, SVC 编号修正)

**Phase 4** — Game Loader (Complete)
- [x] NRO loader (homebrew)
- [x] NSO loader (retail)
- [x] NPDM parser
- [x] NSP/XCI package extraction
- [x] AES cryptography engine
- [x] Key manager
- [x] RomFS extraction

**Phase 5** — Frontend UI (Complete)
- [x] AppKit application skeleton
- [x] Game library grid
- [x] Library sidebar with filtering/search
- [x] Game window with MTKView
- [x] Debug panel (GPU stats, memory, shaders)
- [x] Settings panel
- [x] Log panel
- [x] Screenshot support

**Phase 6** — Audio & Input (Complete)
- [x] CoreAudio audio backend
- [x] GameController input handling
- [x] Keyboard/mouse HID support

**Phase 7** — Build System & Testing (Complete)
- [x] CMake build system (Debug/Release/ASan)
- [x] SPIRV-Cross integration
- [x] SPIRV-Tools integration
- [x] 74 unit tests (all passing)
- [x] NRO loading tests
- [x] GPU parser tests
- [x] Headless runner (CI-capable)

**Phase 8** — Debugger & Automation (In Progress)
- [x] EmuDebugger: Breakpoint manager (set/clear/list at guest addresses)
- [x] EmuDebugger: CPU register capture (x0-x30, SP, PC, PState)
- [x] EmuDebugger: Memory read/write API
- [x] EmuDebugger: Execution control (pause/continue/step)
- [x] Debug panel: 5-tab UI (GPU/CPU/Memory/Breakpoints/Log)
- [x] Debug panel: CPU register viewer (35 registers)
- [x] Debug panel: Memory hex viewer with address input
- [x] Debug panel: Breakpoint management UI
- [x] Debug panel: Execution control buttons
- [x] volt-agent: CLI automation & test system
- [x] volt-agent: manifest.json test definitions
- [x] volt-agent: JSON report output (CI-ready)
- [x] volt-agent: Watch mode for development
- [x] TraceEngine: 全链路追踪 (SVC/IPC/GPU/Thread 通道, mmap 环形缓冲, 零分配写入)
- [x] TraceEngine: 通道级开关 + 过滤器 + JSON 查询接口
- [x] TraceEngine: SVC 调度双向追踪 (进入+返回值)
- [x] TraceEngine: IPC 请求/响应追踪
- [x] TraceEngine: GPU Draw Call 追踪 (方法+管线键)
- [x] TraceEngine: Thread 生命周期追踪 (create/start/exit)
- [x] DebugServer: Unix domain socket 调试接口 (/tmp/m1switch_debug.sock)
- [x] DebugServer: JSON-RPC 命令协议 (break/continue/step/regs/read/write/trace/snap/stats)
- [x] SnapshotManager: 完整状态快照 (CPU 寄存器 + SVC 历史)
- [x] SnapshotManager: 自动快照 (按 SVC 间隔或帧间隔)

**Phase 9** — E2E Test Suite & NRO Compatibility (In Progress)
- [x] devkitPro + libnx 工具链集成
- [x] 分层测试 NRO 架构 (tests/e2e/): L0 裸 syscall / L1 单服务
- [x] L0 纯汇编测试框架 (绕过 libnx crt0)
- [x] NRO 加载器 PIE 格式兼容 (ADRP 重定位修补 + MOD0/DYNAMIC/RELR 偏移修正)
- [x] 16K 页冲突修复 text/data 分段映射
- [x] test_svc_exit: 最小 SVC #0x07 退出测试通过
- [ ] L0 其余测试: test_svc_heap, test_svc_thread, test_svc_sleep, test_svc_output
- [ ] L1 服务测试: sm_getsrv, fs_romfs, set_sys, hid_npad, vi_init, applet_basic
- [ ] switch-examples 自动编译 & 全量回归
- [ ] CI 集成 devkitPro 编译步骤

## E2E Test Suite

```
tests/e2e/
├── Makefile              ← 顶层构建入口
├── build_all.sh          ← 一键构建脚本
├── common/
│   ├── framework.h       ← 测试宏 (TEST_PASS/FAIL/SKIP/INFO + 断言)
│   └── common.mk         ← 共享构建规则 (devkitA64)
├── l0_svc_exit/
│   ├── main.S            ← 纯汇编: svcExitProcess
│   └── Makefile
├── l0_svc_heap/          ← 内存分配 + 模式写入/校验
├── l0_svc_thread/        ← 线程创建 + 同步
├── l0_svc_sleep/         ← 定时器精度
├── l0_svc_output/        ← 调试输出通道
├── l1_sm_getsrv/         ← 服务发现测试 (37+ 服务)
├── l1_fs_romfs/          ← RomFS 文件读取
├── l1_set_sys/           ← 系统设置读取
├── l1_hid_npad/          ← HID 手柄初始化
├── l1_vi_init/           ← 帧缓冲初始化
└── l1_applet_basic/      ← Applet 生命周期
```

构建: `DEVKITPRO=/opt/devkitpro make -C tests/e2e`

## License

MIT

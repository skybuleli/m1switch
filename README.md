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

**Phase 8** — Debugger & Automation (New)
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

## License

MIT

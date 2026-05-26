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

## License

MIT

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
- [ ] MTKView window
- [ ] Metal device initialization
- [ ] AppKit application skeleton

## License

MIT

# Architecture

## Overview

```
                    AppKit UI Layer
             (Game Library / Window / HUD / Settings)
                         │
              ┌──────────┴──────────┐
              │   Emulator Core      │
              │   (Scheduler / Sync)  │
              │                      │
  ┌───────────┼──────────┬───────────┤
  │           │          │           │
 CPU        GPU      Audio      Input
 (ARM64   (Maxwell  (ADPCM →   (HID →
  Native)   → Metal)  CoreAU)   GCCtrl)
  │           │          │           │
  └───────────┴──────────┴───────────┘
              │
         Horizon OS HLE
    (FS / AM / SM / VI / 50+ services)
              │
         Loader + Crypto
    (NSP/NCA/NPDM/RomFS/prod.keys)
              │
         Memory (UMA / 4GB map)
```

## Key Design Decisions

### 1. Native ARM64 Execution (Patch-and-Trap)

Unlike traditional emulators (Yuzu: ARM64→x64 JIT, Ryujinx: ARM64→x64 JIT),
M1Switch runs Switch game code **natively** on M1's ARM64 cores.

**Mechanism:**
1. Load game binary into mapped executable memory (`MAP_JIT`)
2. Scan for all `SVC #imm` instructions, replace with `BRK #tag`
3. Set up Mach exception handler on each guest thread
4. Jump to guest entry point
5. On `BRK` → exception port fires → parse SVC number → dispatch to HLE

**Why:** M1 Cortex-A57 performance ratio is ~4-5:1. No JIT overhead,
no code cache management, no self-modifying code issues.

### 2. UMA Zero-Copy GPU

M1's Unified Memory Architecture means the guest 4GB RAM can be
mapped directly as an `MTLBuffer`. Textures, vertex data, and
uniforms placed by the game in guest memory are **immediately**
visible to the GPU without any `memcpy` or DMA transfer.

### 3. Metal-Only GPU Backend

No OpenGL, no Vulkan. One GPU API, one optimization target.
Shaders go through: Maxwell ISA → SPIR-V → SPIRV-Cross → MSL.

### 4. SPIRV-Cross for Shader Translation

Instead of writing a full Maxwell→MSL compiler, we target
SPIR-V (standard intermediate format) and use Khronos's
SPIRV-Cross to convert to MSL. This saves ~4-5 months of
development on the shader code generator.

## Reference Sources

| Source | Use |
|---|---|
| **deko3d** (`engine_*.def`) | Maxwell register/method definitions |
| **Ryujinx** (C#) | GPU state machine, shader IR, service HLE |
| **Atmosphere-NX** | Real Horizon OS service behavior (oracle) |
| **envytools** (`gm107.c`) | Maxwell ISA disassembly rules |
| **SPIRV-Cross** | SPIR-V → MSL conversion library |
| **LibHac** | NCA/NSP/RomFS/SaveData file format |

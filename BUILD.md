# Build Guide

## Prerequisites

- **macOS Sonoma 14+** (Apple Silicon required)
- **Xcode 15+ Command Line Tools** (or Xcode.app)
- **CMake 3.20+**

Verify:

```bash
xcode-select -p                    # Should show /Applications/Xcode.app/... or /Library/Developer/CommandLineTools
c++ --version                      # AppleClang 15+ with arm64 target
cmake --version                    # 3.20+
```

## Quick Start

```bash
# Clone (if using git)
git clone https://github.com/user/m1switch.git
cd m1switch

# Configure
cmake -B build -S .

# Build
cmake --build build

# Run (from build directory)
open build/m1switch.app
# Or
./build/m1switch.app/Contents/MacOS/m1switch
```

## Build Configurations

```bash
# Debug build (default)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release

# ASan (Address Sanitizer) build
cmake -B build -DCMAKE_BUILD_TYPE=ASan

# Xcode project generation (optional)
cmake -B build -G Xcode
open build/m1switch.xcodeproj
```

## Build Options

| Option | Default | Description |
|---|---|---|
| `M1SWITCH_BUILD_TESTS` | ON | Build test targets |
| `M1SWITCH_USE_SPIRV` | OFF | Enable SPIRV-Cross/SPIRV-Tools for shader compilation |

Example:

```bash
cmake -B build -DM1SWITCH_USE_SPIRV=ON -DM1SWITCH_BUILD_TESTS=OFF
```

## Third-party Dependencies

| Library | Required? | Integration |
|---|---|---|
| Metal-Cpp | ✅ System SDK | Bundled with Xcode |
| SPIRV-Cross | Phase 4+ | `git submodule add https://github.com/KhronosGroup/SPIRV-Cross.git third_party/SPIRV-Cross` |
| SPIRV-Tools | Phase 4+ | `git submodule add https://github.com/KhronosGroup/SPIRV-Tools.git third_party/SPIRV-Tools` |

To clone all third-party dependencies:

```bash
git submodule update --init --recursive
```

## Project Structure

```
m1switch/
├── CMakeLists.txt      # Top-level build
├── src/
│   ├── main.mm         # Entry point
│   ├── common/         # Log, Config, Types
│   ├── core/           # Emulator core, scheduler
│   ├── cpu/            # CPU (Patch-and-Trap)
│   ├── gpu/            # GPU command parser + shader + backend
│   ├── services/       # Horizon OS service HLE
│   ├── kernel/         # Kernel SVC HLE
│   ├── loader/         # NSP/NCA/NPDM loader
│   ├── audio/          # Audio (CoreAudio)
│   ├── input/          # Input (GameController)
│   └── frontend/       # UI (AppKit + MTKView)
├── tests/
│   ├── unit/           # Unit tests
│   ├── integration/    # Integration tests
│   └── e2e/            # End-to-end smoke tests
├── third_party/        # Git submodules
└── docs/               # Documentation
```

## Troubleshooting

### `<cstdint> not found`

Xcode 16+/Command Line Tools for macOS Sequoia moved the C++ standard library headers. CMake auto-detects and adds the correct include path. If you see this issue with manual builds:

```bash
# Ensure you have the latest Command Line Tools
sudo rm -rf /Library/Developer/CommandLineTools
xcode-select --install
```

### Linker errors with UTType

Add `-framework UniformTypeIdentifiers` or use the CMake target which already includes it.

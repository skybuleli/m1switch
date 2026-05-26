#pragma once

#include "common/Types.h"
#include "gpu/shader/ShaderIr.h"

#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════
// Metal Shader Compiler Bridge
// ═══════════════════════════════════════════════════════════
//
// Public API for the Metal shader compilation pipeline.
// These functions are implemented in MetalShaderCompiler.mm.

// ── SPIR-V → MSL conversion ────────────────────────────────
// Converts SPIR-V binary to MSL source code.
// If SPIRV-Cross is available, uses it; otherwise generates passthrough MSL.
// Returns empty string on error (details in error_out).
std::string MetalShaderCompile(const std::vector<u32>& spirv,
                                ShaderStage stage,
                                std::string& error_out);

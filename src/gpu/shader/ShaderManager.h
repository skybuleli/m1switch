#pragma once

#include "gpu/shader/ShaderIr.h"
#include "gpu/shader/MaxwellDecoder.h"
#include "gpu/shader/SpirvEmitter.h"
#include "gpu/shader/ShaderCache.h"
#include "gpu/maxwell/GpuState.h"

#include <vector>
#include <string>
#include <functional>

// ObjC forward declarations (avoid forcing Metal headers on C++ consumers)
#ifdef __OBJC__
@protocol MTLFunction;
@protocol MTLRenderPipelineState;
typedef id<MTLFunction> MTLFunctionRef;
typedef id<MTLRenderPipelineState> MTLPipelineRef;
#else
typedef void* MTLFunctionRef;
typedef void* MTLPipelineRef;
#endif

// ═══════════════════════════════════════════════════════════
// Shader Manager
// ═══════════════════════════════════════════════════════════
//
// Top-level shader compilation orchestrator.
// Manages the pipeline: Extract → Decode → SPIR-V → MSL → Compile
//
// Thread-safe for async compilation.

// Forward declare MetalDevice
class MetalDevice;

// ── Compilation result ─────────────────────────────────────
struct ShaderCompileResult {
    bool success = false;
    std::string error_msg;
    u64 hash = 0;
    ShaderStage stage = ShaderStage::Fragment;

    // Compiled shader functions (Metal)
    MTLFunctionRef vertex_function = nullptr;
    MTLFunctionRef fragment_function = nullptr;

    // Pipeline state for combined V+F shaders
    MTLPipelineRef pipeline_state = nullptr;

    // Debug info
    std::string msl_source;
    size_t spirv_word_count = 0;
    size_t ir_instruction_count = 0;
};

// ── Shader compilation callback ───────────────────────────
using ShaderCompileCallback = std::function<void(const ShaderCompileResult&)>;

// ── ShaderManager ─────────────────────────────────────────
class ShaderManager {
public:
    explicit ShaderManager(MetalDevice& device);
    ~ShaderManager();

    // Initialize the shader manager
    Result Initialize();

    // ── High-level compilation ────────────────────────

    // Compile a single shader from guest memory
    // program_region: pointer to the GPU program region in guest memory
    // shader_state: ShaderState describing the shader to compile
    // callback: optional async callback when compilation completes
    ShaderCompileResult CompileShader(const u8* program_region,
                                       u64 region_size,
                                       const ShaderState& shader_state);

    // Compile all shaders from the GPU state
    // Returns number of shaders compiled
    u32 CompileAllShaders(const u8* program_region,
                           u64 region_size,
                           const ShaderState shaders[MAX_SHADER_STAGES]);

    // ── Pipeline creation ─────────────────────────────

    // Create a combined render pipeline for vertex + fragment shader
    ShaderCompileResult CreateRenderPipeline(
        const ShaderCompileResult& vertex,
        const ShaderCompileResult& fragment);

    // ── Accessors ────────────────────────────────────

    ShaderCache& GetCache() { return cache_; }
    const ShaderCache& GetCache() const { return cache_; }

    // ── Debug / stats ────────────────────────────────
    void DumpStats() const;
    void InvalidateCache();

private:
    MetalDevice& device_;
    ShaderCache cache_;
    MaxwellDecoder decoder_;
    bool initialized_ = false;

    // Internal compilation steps
    ShaderProgram DecodeShader(const u8* data, u32 size, ShaderStage stage);

    // SPIR-V emission + cross-compilation to MSL
    CompiledShader CrossCompile(const ShaderProgram& program);

    // Metal compilation (MSL source → MTLFunction)
    MTLFunctionRef CompileMSL(const std::string& msl_source,
                               ShaderStage stage,
                               std::string& error_out);

    // Combined pipeline creation
    MTLPipelineRef CreatePipeline(
        MTLFunctionRef vert_fn,
        MTLFunctionRef frag_fn,
        std::string& error_out);
};

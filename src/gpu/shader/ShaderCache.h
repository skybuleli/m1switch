#pragma once

#include "gpu/shader/ShaderIr.h"
#include "gpu/shader/MaxwellDecoder.h"
#include "common/Log.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <functional>

// ═══════════════════════════════════════════════════════════
// Shader Cache
// ═══════════════════════════════════════════════════════════
//
// Caches decoded ShaderPrograms and compiled MSL shaders
// by their content hash. Thread-safe.

// ── Compiled shader result ─────────────────────────────────
struct CompiledShader {
    u64 hash = 0;
    ShaderStage stage = ShaderStage::Fragment;
    bool valid = false;

    // MSL source code (output of SPIRV-Cross)
    std::string msl_source;

    // SPIR-V binary (intermediate)
    std::vector<u32> spirv_binary;

    // Metal function handles (after compilation)
    void* vertex_function = nullptr;   // id<MTLFunction>
    void* fragment_function = nullptr; // id<MTLFunction>

    // Debug info
    std::string debug_disassembly;
};

// ── Cached decoded program entry ──────────────────────────
struct DecodedEntry {
    ShaderProgram program;
    bool decoded = false;
};

// ── ShaderCache singleton-like class ──────────────────────
class ShaderCache {
public:
    // Find a previously decoded program by hash
    ShaderProgram* FindDecoded(u64 hash);

    // Find a previously compiled shader by hash
    CompiledShader* FindCompiled(u64 hash);

    // Store a decoded program
    void StoreDecoded(const ShaderProgram& program);

    // Store a compiled shader result
    void StoreCompiled(u64 hash, const CompiledShader& compiled);

    // Pipeline state cache (Metal MTLRenderPipelineState)
    void StorePipeline(u64 hash, void* pipeline_state);
    void* FindPipeline(u64 hash);

    // Clear all caches
    void Clear();

    // Stats
    size_t DecodedCount() const { std::lock_guard l(mutex_); return decoded_.size(); }
    size_t CompiledCount() const { std::lock_guard l(mutex_); return compiled_.size(); }
    size_t PipelineCount() const { std::lock_guard l(mutex_); return pipelines_.size(); }

private:
    mutable std::mutex mutex_;
    std::unordered_map<u64, DecodedEntry> decoded_;
    std::unordered_map<u64, CompiledShader> compiled_;
    std::unordered_map<u64, void*> pipelines_;
};

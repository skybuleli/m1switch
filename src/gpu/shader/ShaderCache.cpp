#include "gpu/shader/ShaderCache.h"
#include "common/Log.h"

// ── Find decoded program ─────────────────────────────────
ShaderProgram* ShaderCache::FindDecoded(u64 hash) {
    std::lock_guard l(mutex_);
    auto it = decoded_.find(hash);
    if (it != decoded_.end() && it->second.decoded) {
        return &it->second.program;
    }
    return nullptr;
}

// ── Find compiled shader ─────────────────────────────────
CompiledShader* ShaderCache::FindCompiled(u64 hash) {
    std::lock_guard l(mutex_);
    auto it = compiled_.find(hash);
    if (it != compiled_.end() && it->second.valid) {
        return &it->second;
    }
    return nullptr;
}

// ── Store decoded program ────────────────────────────────
void ShaderCache::StoreDecoded(const ShaderProgram& program) {
    std::lock_guard l(mutex_);
    auto& entry = decoded_[program.hash];
    entry.program = program;
    entry.decoded = true;
    LOG_DEBUG("ShaderCache: stored decoded program hash=0x%016llx (%s)",
              program.hash, ShaderStageName(program.stage));
}

// ── Store compiled shader ────────────────────────────────
void ShaderCache::StoreCompiled(u64 hash, const CompiledShader& compiled) {
    std::lock_guard l(mutex_);
    compiled_[hash] = compiled;
    LOG_INFO("ShaderCache: stored compiled shader hash=0x%016llx (%s, %zu bytes MSL)",
             hash, ShaderStageName(compiled.stage), compiled.msl_source.size());
}

// ── Pipeline cache ───────────────────────────────────────
void ShaderCache::StorePipeline(u64 hash, void* pipeline_state) {
    std::lock_guard l(mutex_);
    pipelines_[hash] = pipeline_state;
    LOG_DEBUG("ShaderCache: stored pipeline hash=0x%016llx", hash);
}

void* ShaderCache::FindPipeline(u64 hash) {
    std::lock_guard l(mutex_);
    auto it = pipelines_.find(hash);
    if (it != pipelines_.end()) {
        return it->second;
    }
    return nullptr;
}

// ── Clear ────────────────────────────────────────────────
void ShaderCache::Clear() {
    std::lock_guard l(mutex_);
    decoded_.clear();
    compiled_.clear();
    pipelines_.clear();
    LOG_INFO("ShaderCache: cleared all entries");
}

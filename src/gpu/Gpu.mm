#include "gpu/Gpu.h"
#include <chrono>

// ═══════════════════════════════════════════════════════════
// GPU Coordinator Implementation
// ═══════════════════════════════════════════════════════════

// Active GPU instance tracking (for debug panel C APIs)
static Gpu* g_active_gpu = nullptr;

Gpu::Gpu()
    : renderer_(device_)
    , shader_mgr_(device_)
    , texture_cache_(device_.Device()) {
    g_active_gpu = this;
}

Gpu::~Gpu() {
    if (g_active_gpu == this) g_active_gpu = nullptr;
}

// ── Initialize ─────────────────────────────────────────────

Result Gpu::Initialize() {
    if (initialized_) return Result::Success;

    LOG_INFO("Gpu: initializing...");

    // 1. Initialize Metal backend
    if (!device_.Device()) {
        LOG_ERROR("Gpu: No Metal device available");
        return Result::GpuShaderCompileFailed;
    }

    // 2. Initialize Metal renderer
    Result r = renderer_.Initialize();
    if (Failed(r)) {
        LOG_ERROR("Gpu: Metal renderer initialization failed");
        return r;
    }

    // 3. Initialize shader manager
    r = shader_mgr_.Initialize();
    if (Failed(r)) {
        LOG_ERROR("Gpu: Shader manager initialization failed");
        return r;
    }

    // 4. Wire renderer to tracker and shader manager
    renderer_.SetStateTracker(&tracker_);
    renderer_.SetShaderManager(&shader_mgr_);
    renderer_.SetTextureCache(&texture_cache_);

    // 5. Set up the draw callback on StateTracker
    // StateTracker::OnMethod already handles draw detection — we hook into
    // the dirty flag polling path via RenderFrame()
    // No additional wiring needed since RenderFrame checks tracker_->IsDirty()

    LOG_INFO("Gpu: initialized successfully");
    initialized_ = true;
    return Result::Success;
}

void Gpu::SetMemory(Memory* mem) {
    memory_ = mem;
    tracker_.SetMemory(mem);
}

// ── Pushbuffer processing ─────────────────────────────────

void Gpu::SubmitPushBuffer(std::span<const u32> words) {
    if (!initialized_ || words.empty()) return;
    tracker_.PushBuffer(words);
}

void Gpu::ProcessEntry(u32 method, u32 arg) {
    if (!initialized_) return;
    tracker_.GetGPFifo().ProcessEntry(method, arg);
}

// ── Render frame ──────────────────────────────────────────

void Gpu::RenderFrame(id<MTLCommandBuffer> cmdBuf,
                      MTLRenderPassDescriptor* passDesc) {
    if (!initialized_ || !cmdBuf || !passDesc) return;

    // MetalRenderer::RenderFrame internally calls BindGameTextures before draws
    renderer_.RenderFrame(cmdBuf, passDesc);

    texture_cache_.EndFrame();
    renderer_.SetTextureCacheStats(texture_cache_.Count(),
                                    texture_cache_.MemoryUsed());
}

// ── Draw callback ─────────────────────────────────────────

void Gpu::OnDraw() {
    if (!initialized_) return;

    LOG_DEBUG("Gpu: draw triggered");

    const auto& state = tracker_.GetState3D();
    if (state.program_region == 0) {
        LOG_DEBUG("Gpu: no program region set, skipping draw");
        return;
    }

    auto* mem = tracker_.GetMemory();
    if (!mem) return;

    u8* prog_ptr = mem->Pointer(state.program_region);
    if (!prog_ptr) {
        LOG_WARN("Gpu: program region 0x%llx not mapped", state.program_region);
        return;
    }

    u32 compiled = shader_mgr_.CompileAllShaders(prog_ptr, 16 * 1024 * 1024,
                                                   state.shaders);
    if (compiled > 0) {
        LOG_INFO("Gpu: compiled %u shaders for draw", compiled);
    }
}

// ── C API for debug panels ──────────────────────────────────

extern "C" {

double Gpu_GetFps() {
    // Approximate FPS: called from Settings/Debug panels
    // Real FPS tracking would be in MetalRenderer
    static auto last = std::chrono::steady_clock::now();
    static double fps = 60.0;
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - last).count();
    if (dt > 0.01) {
        fps = 1.0 / dt;
        // Smooth
        fps = fps * 0.1 + 60.0 * 0.9;
        last = now;
    }
    return fps;
}

size_t Gpu_GetShaderCount() {
    if (!g_active_gpu) return 0;
    return g_active_gpu->GetShaderMgr().GetCache().CompiledCount();
}

size_t Gpu_GetTextureCacheCount() {
    if (!g_active_gpu) return 0;
    return g_active_gpu->GetRenderer().GetTextureCacheCount();
}

size_t Gpu_GetTextureCacheMemory() {
    if (!g_active_gpu) return 0;
    return g_active_gpu->GetRenderer().GetTextureCacheMemory();
}

void Gpu_DumpStats() {
    if (g_active_gpu) g_active_gpu->DumpStats();
}

void ShaderCache_Invalidate() {
    if (g_active_gpu) g_active_gpu->GetShaderMgr().InvalidateCache();
}

} // extern "C"

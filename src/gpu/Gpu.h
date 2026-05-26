#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include "gpu/StateTracker.h"
#include "gpu/backend/MetalDevice.h"
#include "gpu/backend/MetalRenderer.h"
#include "gpu/shader/ShaderManager.h"
#include "gpu/texture/TextureCache.h"

#include <functional>

// ═══════════════════════════════════════════════════════════
// GPU Coordinator
// ═══════════════════════════════════════════════════════════
//
// Top-level GPU orchestrator that owns and coordinates:
//   MetalDevice   → Metal resource creation
//   MetalRenderer → Metal draw commands
//   ShaderManager → Shader compilation (Maxwell → SPIR-V → MSL)
//   StateTracker  → GPFifo parsing → Engine state tracking
//
// The Gpu class bridges the gap between guest GPU commands
// and host Metal rendering.

class Gpu {
public:
    Gpu();
    ~Gpu();

    // ── Lifecycle ──────────────────────────────────────

    // Initialize all GPU subsystems. Must be called before first use.
    Result Initialize();

    // Set the memory instance (must be set before processing commands)
    void SetMemory(Memory* mem);

    // ── Command processing ─────────────────────────────

    // Process a pushbuffer submission from guest (NV SubmitGpfifo)
    void SubmitPushBuffer(std::span<const u32> words);

    // Process a single GPFifo entry
    void ProcessEntry(u32 method, u32 arg);

    // ── Per-frame operations ───────────────────────────

    // Called once per frame from the MTKView draw loop.
    // Compiles shaders on demand and issues Metal draw commands.
    void RenderFrame(id<MTLCommandBuffer> cmdBuf,
                     MTLRenderPassDescriptor* passDesc);

    // ── State access ──────────────────────────────────

    MetalDevice&       GetDevice()       { return device_; }
    MetalRenderer&     GetRenderer()     { return renderer_; }
    ShaderManager&     GetShaderMgr()    { return shader_mgr_; }
    StateTracker&      GetTracker()      { return tracker_; }
    TextureCache&      GetTextureCache() { return texture_cache_; }
    const StateTracker& GetTracker() const { return tracker_; }

    // Debug helpers
    void SetTestTriangle() { renderer_.SetTestTriangle(); }
    void DumpStats() const { shader_mgr_.DumpStats(); }

private:
    // Called by StateTracker when a draw command is detected
    void OnDraw();

    MetalDevice     device_;
    MetalRenderer   renderer_;
    ShaderManager   shader_mgr_;
    StateTracker    tracker_;
    TextureCache    texture_cache_;
    Memory*         memory_ = nullptr;
    
    bool initialized_ = false;
};

// ── Define GPU_SHARED_MEM_BASE for HID/VI shared memory ─
#ifndef GPU_SHARED_MEM_BASE
#define GPU_SHARED_MEM_BASE 0xE0000000ULL
#endif

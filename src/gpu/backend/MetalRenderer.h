#pragma once

#include "common/Types.h"
#include "gpu/StateTracker.h"
#include "gpu/backend/MetalDevice.h"
#include <Metal/Metal.h>

// ── Metal Renderer ──────────────────────────────────────────
// Connects GpuState3D → Metal draw calls.
// Phase 5: renders a hard-coded triangle to verify the pipeline.

class MetalRenderer {
public:
    MetalRenderer(MetalDevice& device);
    ~MetalRenderer();

    // Initialize shaders and pipeline state
    Result Initialize();

    // Render a frame using the current GPU state
    // @param state     Current 3D engine state from StateTracker
    // @param cmdBuf    Command buffer to encode into
    // @param passDesc  Render pass descriptor (from MTKView drawable)
    void RenderFrame(const GpuState3D& state,
                     id<MTLCommandBuffer> cmdBuf,
                     MTLRenderPassDescriptor* passDesc);

    // ── Test triangle helpers (Phase 5) ───────────────
    void SetTestTriangle();

private:
    MetalDevice& device_;
    id<MTLRenderPipelineState> pipeline_ = nil;
    id<MTLBuffer> vertex_buffer_ = nil;
    bool initialized_ = false;
};

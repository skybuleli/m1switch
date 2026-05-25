#pragma once

#include "common/Types.h"
#include "gpu/StateTracker.h"
#include "gpu/backend/MetalDevice.h"
#include <Metal/Metal.h>

// ── Metal Renderer ──────────────────────────────────────────
// Connects GpuState3D → Metal draw calls.
// Phase P0: reads VBO from guest memory, issues draw calls.

class MetalRenderer {
public:
    MetalRenderer(MetalDevice& device);
    ~MetalRenderer();

    Result Initialize();
    void SetStateTracker(StateTracker* tracker) { tracker_ = tracker; }

    // Render the current GPU state from the tracker
    void RenderFrame(id<MTLCommandBuffer> cmdBuf,
                     MTLRenderPassDescriptor* passDesc);

    // Phase 5 test triangle
    void SetTestTriangle();

private:
    void RenderTriangle(id<MTLRenderCommandEncoder> enc);
    void RenderFromGpuState(id<MTLRenderCommandEncoder> enc);

    MetalDevice& device_;
    StateTracker* tracker_ = nullptr;
    id<MTLRenderPipelineState> pipeline_ = nil;
    id<MTLBuffer> vertex_buffer_ = nil;
    bool initialized_ = false;
};

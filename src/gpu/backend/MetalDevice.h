#pragma once

#include "common/Types.h"
#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>

// ── Metal device + resource manager ─────────────────────────
// Owns MTLDevice, MTLCommandQueue, library compilation.
// Phase 5: minimal implementation for triangle rendering.

class MetalDevice {
public:
    MetalDevice();
    ~MetalDevice();

    // Get the Metal device
    id<MTLDevice> Device() const { return device_; }
    id<MTLCommandQueue> Queue() const { return queue_; }

    // Compile a shader library from source
    id<MTLLibrary> CompileLibrary(const char* source) const;

    // Create a render pipeline state from descriptor
    id<MTLRenderPipelineState> CreateRenderPipeline(
        MTLRenderPipelineDescriptor* desc) const;

private:
    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> queue_ = nil;
};

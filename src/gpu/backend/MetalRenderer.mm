#import "gpu/backend/MetalRenderer.h"
#import "gpu/backend/shaders.h"
#include "common/Log.h"

MetalRenderer::MetalRenderer(MetalDevice& device) : device_(device) {}

MetalRenderer::~MetalRenderer() {
    [pipeline_ release];
    [vertex_buffer_ release];
}

Result MetalRenderer::Initialize() {
    id<MTLLibrary> lib = device_.CompileLibrary(kMetalShaders);
    if (!lib) return Result::GpuShaderCompileFailed;

    id<MTLFunction> vertFn = [lib newFunctionWithName:@"vertex_main"];
    id<MTLFunction> fragFn = [lib newFunctionWithName:@"fragment_main"];
    if (!vertFn || !fragFn) return Result::GpuShaderCompileFailed;

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vertFn;
    desc.fragmentFunction = fragFn;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    desc.depthAttachmentPixelFormat = MTLPixelFormatInvalid;

    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat4;
    vd.attributes[1].offset = 8;
    vd.attributes[1].bufferIndex = 0;
    vd.layouts[0].stride = 24;
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    desc.vertexDescriptor = vd;

    pipeline_ = device_.CreateRenderPipeline(desc);
    [desc release]; [vd release]; [vertFn release]; [fragFn release]; [lib release];

    if (!pipeline_) return Result::GpuShaderCompileFailed;
    initialized_ = true;
    LOG_INFO("Metal renderer ready");
    return Result::Success;
}

void MetalRenderer::SetTestTriangle() {
    struct TriVertex { float pos[2]; float color[4]; };
    const TriVertex verts[] = {
        {{-0.5, -0.5}, {1.0, 0.0, 0.0, 1.0}},
        {{ 0.5, -0.5}, {0.0, 1.0, 0.0, 1.0}},
        {{ 0.0,  0.5}, {0.0, 0.0, 1.0, 1.0}},
    };
    if (vertex_buffer_) [vertex_buffer_ release];
    vertex_buffer_ = [device_.Device() newBufferWithBytes:verts
                                                    length:sizeof(verts)
                                                   options:MTLResourceStorageModeShared];
}

void MetalRenderer::RenderFrame(id<MTLCommandBuffer> cmdBuf,
                                 MTLRenderPassDescriptor* passDesc) {
    if (!initialized_ || !passDesc) return;
    id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:passDesc];
    [enc setRenderPipelineState:pipeline_];

    // If we have a tracker with pending draws, render from GPU state
    // Otherwise render the test triangle
    if (tracker_ && tracker_->IsDirty()) {
        RenderFromGpuState(enc);
        tracker_->ClearDirty();
    } else if (vertex_buffer_) {
        RenderTriangle(enc);
    }

    [enc endEncoding];
}

void MetalRenderer::RenderTriangle(id<MTLRenderCommandEncoder> enc) {
    [enc setVertexBuffer:vertex_buffer_ offset:0 atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void MetalRenderer::RenderFromGpuState(id<MTLRenderCommandEncoder> enc) {
    const auto& state = tracker_->GetState3D();

    // ── Viewport ──────────────────────────────────────
    if (state.viewport_transform_enable && state.viewports[0].scale_x > 0) {
        MTLViewport vp;
        // The viewport transform in Maxwell uses scale + translate
        // For Phase P0: use raw values from state
        vp.originX = state.viewports[0].translate_x;
        vp.originY = state.viewports[0].translate_y;
        vp.width   = state.viewports[0].scale_x * 2.0f;
        vp.height  = state.viewports[0].scale_y * 2.0f;
        vp.znear   = 0.0;
        vp.zfar    = 1.0;
        [enc setViewport:vp];
    }

    // ── Scissor ───────────────────────────────────────
    if (state.scissors[0].enabled) {
        MTLScissorRect sc;
        sc.x      = state.scissors[0].min_x;
        sc.y      = state.scissors[0].min_y;
        sc.width  = state.scissors[0].max_x - state.scissors[0].min_x;
        sc.height = state.scissors[0].max_y - state.scissors[0].min_y;
        [enc setScissorRect:sc];
    }

    // ── Draw call (Phase P0: basic non-indexed draw) ──
    u32 count = state.draw_arrays_count;
    if (count > 0) {
        // Use guest VBO memory directly (UMA zero-copy)
        auto* mem = tracker_->GetMemory();
        if (mem) {
            for (int i = 0; i < 16; i++) {
                const auto& va = state.vertex_arrays[i];
                if (va.enabled && va.address > 0) {
                    u8* ptr = mem->Pointer(va.address);
                    if (ptr) {
                        id<MTLBuffer> buf = [device_.Device()
                            newBufferWithBytesNoCopy:ptr
                                              length:va.stride * count
                                             options:MTLResourceStorageModeShared
                                         deallocator:nil];
                        [enc setVertexBuffer:buf offset:0 atIndex:i];
                        [buf release];  // retained by encoder
                    }
                }
            }
        }

        // Draw
        u32 first = state.draw_arrays_first;
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:first
                vertexCount:count > 0 ? count : 3];

        LOG_INFO("Rendered from GpuState: %u vertices", count);
    }
}

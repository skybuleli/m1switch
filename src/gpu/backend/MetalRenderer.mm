#import "gpu/backend/MetalRenderer.h"
#import "gpu/backend/shaders.h"
#include "common/Log.h"

MetalRenderer::MetalRenderer(MetalDevice& device) : device_(device) {}

MetalRenderer::~MetalRenderer() {
    [pipeline_ release];
    [vertex_buffer_ release];
}

Result MetalRenderer::Initialize() {
    // ── Compile shaders ──────────────────────────────
    id<MTLLibrary> lib = device_.CompileLibrary(kMetalShaders);
    if (!lib) {
        LOG_ERROR("Failed to compile shader library");
        return Result::GpuShaderCompileFailed;
    }

    id<MTLFunction> vertFn = [lib newFunctionWithName:@"vertex_main"];
    id<MTLFunction> fragFn = [lib newFunctionWithName:@"fragment_main"];
    if (!vertFn || !fragFn) {
        LOG_ERROR("Failed to find shader functions");
        return Result::GpuShaderCompileFailed;
    }

    // ── Create pipeline state ────────────────────────
    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vertFn;
    desc.fragmentFunction = fragFn;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    desc.depthAttachmentPixelFormat = MTLPixelFormatInvalid;

    // Vertex descriptor: position(float2) + color(float4)
    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat4;
    vd.attributes[1].offset = 8;  // after float2
    vd.attributes[1].bufferIndex = 0;
    vd.layouts[0].stride = 24;  // float2 + float4 = 8 + 16 = 24
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    desc.vertexDescriptor = vd;

    pipeline_ = device_.CreateRenderPipeline(desc);
    [desc release];
    [vd release];
    [vertFn release];
    [fragFn release];
    [lib release];

    if (!pipeline_) {
        LOG_ERROR("Failed to create render pipeline");
        return Result::GpuShaderCompileFailed;
    }

    initialized_ = true;
    LOG_INFO("Metal renderer initialized with pipeline");
    return Result::Success;
}

void MetalRenderer::SetTestTriangle() {
    // Hardcoded triangle (Phase 5 verification)
    // Position (float2) + Color (float4) interleaved
    struct TriVertex { float pos[2]; float color[4]; };
    const TriVertex verts[] = {
        {{-0.5, -0.5}, {1.0, 0.0, 0.0, 1.0}},  // Red
        {{ 0.5, -0.5}, {0.0, 1.0, 0.0, 1.0}},  // Green
        {{ 0.0,  0.5}, {0.0, 0.0, 1.0, 1.0}},  // Blue
    };

    if (vertex_buffer_)[vertex_buffer_ release];
    vertex_buffer_ = [device_.Device() newBufferWithBytes:verts
                                                    length:sizeof(verts)
                                                   options:MTLResourceStorageModeShared];
    LOG_INFO("Test triangle vertex buffer created (%zu bytes)", sizeof(verts));
}

void MetalRenderer::RenderFrame(const GpuState3D& state,
                                 id<MTLCommandBuffer> cmdBuf,
                                 MTLRenderPassDescriptor* passDesc) {
    if (!initialized_ || !passDesc) return;

    id<MTLRenderCommandEncoder> enc =
        [cmdBuf renderCommandEncoderWithDescriptor:passDesc];

    [enc setRenderPipelineState:pipeline_];

    if (vertex_buffer_) {
        [enc setVertexBuffer:vertex_buffer_ offset:0 atIndex:0];

        // Draw the triangle
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
    } else {
        // No vertex buffer — still need to end encoder
        LOG_TRACE("RenderFrame: no vertex buffer");
    }

    [enc endEncoding];
}

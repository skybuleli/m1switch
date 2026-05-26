#import "gpu/backend/MetalRenderer.h"
#import "gpu/backend/shaders.h"
#include "common/Log.h"
#include <algorithm>

MetalRenderer::MetalRenderer(MetalDevice& device) : device_(device) {}

MetalRenderer::~MetalRenderer() {
    [fallback_pipeline_ release];
    [game_pipeline_ release];
    [depth_stencil_ release];
    [vertex_buffer_ release];
    [fb_pipeline_ release];
    [fb_texture_ release];
    [fb_sampler_ release];
    for (auto& [h, p] : pipeline_cache_) [p release];
    for (auto& [addr, entry] : buffer_cache_) [entry.buffer release];
    for (u32 i = 0; i < MAX_BOUND_TEXTURES; i++) {
        if (game_textures_[i]) [game_textures_[i] release];
        if (game_samplers_[i]) [game_samplers_[i] release];
    }
    for (u32 i = 0; i < MAX_RT; i++) {
        if (rt_textures_[i]) [rt_textures_[i] release];
    }
    if (depth_texture_) [depth_texture_ release];
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

    fallback_pipeline_ = device_.CreateRenderPipeline(desc);
    [desc release]; [vd release]; [vertFn release]; [fragFn release];

    if (!fallback_pipeline_) return Result::GpuShaderCompileFailed;

    MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
    dsDesc.depthCompareFunction = MTLCompareFunctionLess;
    dsDesc.depthWriteEnabled = YES;
    depth_stencil_ = [device_.Device() newDepthStencilStateWithDescriptor:dsDesc];
    [dsDesc release];

    initialized_ = true;
    // ── Initialize framebuffer blit pipeline ──────────
    id<MTLFunction> fbVertFn = [lib newFunctionWithName:@"fb_vertex_main"];
    id<MTLFunction> fbFragFn = [lib newFunctionWithName:@"fb_fragment_main"];
    if (fbVertFn && fbFragFn) {
        MTLRenderPipelineDescriptor* fbDesc = [[MTLRenderPipelineDescriptor alloc] init];
        fbDesc.vertexFunction = fbVertFn;
        fbDesc.fragmentFunction = fbFragFn;
        fbDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        fbDesc.depthAttachmentPixelFormat = MTLPixelFormatInvalid;

        fb_pipeline_ = device_.CreateRenderPipeline(fbDesc);
        [fbDesc release];

        if (fb_pipeline_) {
            // Create linear sampler for framebuffer texture
            MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
            sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
            sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
            sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
            sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            fb_sampler_ = [device_.Device() newSamplerStateWithDescriptor:sampDesc];
            [sampDesc release];
            LOG_INFO("Framebuffer blit pipeline ready");
        }
    }
    [fbVertFn release];
    [fbFragFn release];
    [lib release];

    LOG_INFO("Metal renderer ready");
    return Result::Success;
}

void MetalRenderer::SetGameTexture(u32 index, id<MTLTexture> texture) {
    if (index >= MAX_BOUND_TEXTURES) return;
    if (game_textures_[index]) [game_textures_[index] release];
    game_textures_[index] = texture;
    if (texture) {
        [texture retain];
        has_game_textures_ = true;
    }
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

MTLPrimitiveType MetalRenderer::ToMetalPrimitive(PrimitiveType type) {
    switch (type) {
    case PrimitiveType::Points:         return MTLPrimitiveTypePoint;
    case PrimitiveType::Lines:          return MTLPrimitiveTypeLine;
    case PrimitiveType::LineStrip:      return MTLPrimitiveTypeLineStrip;
    case PrimitiveType::Triangles:      return MTLPrimitiveTypeTriangle;
    case PrimitiveType::TriangleStrip:  return MTLPrimitiveTypeTriangleStrip;
    case PrimitiveType::TriangleFan:    return MTLPrimitiveTypeTriangleStrip;
    default:                            return MTLPrimitiveTypeTriangle;
    }
}

MTLCompareFunction MetalRenderer::ToMetalCompare(CompareOp op) {
    switch (op) {
    case CompareOp::Never:    return MTLCompareFunctionNever;
    case CompareOp::Less:     return MTLCompareFunctionLess;
    case CompareOp::Equal:    return MTLCompareFunctionEqual;
    case CompareOp::Lequal:   return MTLCompareFunctionLessEqual;
    case CompareOp::Greater:  return MTLCompareFunctionGreater;
    case CompareOp::NotEqual: return MTLCompareFunctionNotEqual;
    case CompareOp::Gequal:   return MTLCompareFunctionGreaterEqual;
    case CompareOp::Always:   return MTLCompareFunctionAlways;
    default:                  return MTLCompareFunctionAlways;
    }
}

MTLVertexFormat MetalRenderer::ToMetalVertexFormat(u32 size, u32 type) {
    if (type == 0) {
        switch (size) {
        case 1: return MTLVertexFormatFloat;
        case 2: return MTLVertexFormatFloat2;
        case 3: return MTLVertexFormatFloat3;
        case 4: return MTLVertexFormatFloat4;
        }
    } else if (type == 1) {
        switch (size) {
        case 1: return MTLVertexFormatInt;
        case 2: return MTLVertexFormatInt2;
        case 3: return MTLVertexFormatInt3;
        case 4: return MTLVertexFormatInt4;
        }
    } else if (type == 2) {
        switch (size) {
        case 1: return MTLVertexFormatUInt;
        case 2: return MTLVertexFormatUInt2;
        case 3: return MTLVertexFormatUInt3;
        case 4: return MTLVertexFormatUInt4;
        }
    }
    return MTLVertexFormatFloat4;
}

MTLBlendFactor MetalRenderer::ToMetalBlendFactor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:               return MTLBlendFactorZero;
    case BlendFactor::One:                return MTLBlendFactorOne;
    case BlendFactor::SrcColor:           return MTLBlendFactorSourceColor;
    case BlendFactor::OneMinusSrcColor:   return MTLBlendFactorOneMinusSourceColor;
    case BlendFactor::SrcAlpha:           return MTLBlendFactorSourceAlpha;
    case BlendFactor::OneMinusSrcAlpha:   return MTLBlendFactorOneMinusSourceAlpha;
    case BlendFactor::DstColor:           return MTLBlendFactorDestinationColor;
    case BlendFactor::OneMinusDstColor:   return MTLBlendFactorOneMinusDestinationColor;
    case BlendFactor::DstAlpha:           return MTLBlendFactorDestinationAlpha;
    case BlendFactor::OneMinusDstAlpha:   return MTLBlendFactorOneMinusDestinationAlpha;
    case BlendFactor::SrcAlphaSaturate:   return MTLBlendFactorSourceAlphaSaturated;
    case BlendFactor::ConstantColor:      return MTLBlendFactorBlendColor;
    case BlendFactor::OneMinusConstColor: return MTLBlendFactorOneMinusBlendColor;
    case BlendFactor::ConstantAlpha:      return MTLBlendFactorBlendAlpha;
    case BlendFactor::OneMinusConstAlpha: return MTLBlendFactorOneMinusBlendAlpha;
    default:                              return MTLBlendFactorOne;
    }
}

MTLBlendOperation MetalRenderer::ToMetalBlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add:             return MTLBlendOperationAdd;
    case BlendOp::Subtract:        return MTLBlendOperationSubtract;
    case BlendOp::ReverseSubtract: return MTLBlendOperationReverseSubtract;
    case BlendOp::Min:             return MTLBlendOperationMin;
    case BlendOp::Max:             return MTLBlendOperationMax;
    default:                       return MTLBlendOperationAdd;
    }
}

MTLStencilOperation MetalRenderer::ToMetalStencilOp(StencilOp op) {
    switch (op) {
    case StencilOp::Keep:      return MTLStencilOperationKeep;
    case StencilOp::Zero:      return MTLStencilOperationZero;
    case StencilOp::Replace:   return MTLStencilOperationReplace;
    case StencilOp::IncrClamp: return MTLStencilOperationIncrementClamp;
    case StencilOp::DecrClamp: return MTLStencilOperationDecrementClamp;
    case StencilOp::Invert:    return MTLStencilOperationInvert;
    case StencilOp::IncrWrap:  return MTLStencilOperationIncrementWrap;
    case StencilOp::DecrWrap:  return MTLStencilOperationDecrementWrap;
    default:                   return MTLStencilOperationKeep;
    }
}

void MetalRenderer::ApplyViewport(id<MTLRenderCommandEncoder> enc) {
    if (!tracker_) return;
    const auto& state = tracker_->GetState3D();
    if (state.viewport_transform_enable && state.viewports[0].scale_x > 0) {
        MTLViewport vp;
        vp.originX = state.viewports[0].translate_x - state.viewports[0].scale_x;
        vp.originY = state.viewports[0].translate_y - state.viewports[0].scale_y;
        vp.width   = state.viewports[0].scale_x * 2.0f;
        vp.height  = state.viewports[0].scale_y * 2.0f;
        vp.znear   = 0.0;
        vp.zfar    = 1.0;
        if (vp.width < 1) vp.width = 1;
        if (vp.height < 1) vp.height = 1;
        [enc setViewport:vp];
    }
}

void MetalRenderer::ApplyScissor(id<MTLRenderCommandEncoder> enc) {
    if (!tracker_) return;
    const auto& state = tracker_->GetState3D();
    if (state.scissors[0].enabled) {
        MTLScissorRect sc;
        sc.x      = state.scissors[0].min_x;
        sc.y      = state.scissors[0].min_y;
        sc.width  = state.scissors[0].max_x - state.scissors[0].min_x;
        sc.height = state.scissors[0].max_y - state.scissors[0].min_y;
        if (sc.width < 1) sc.width = 1;
        if (sc.height < 1) sc.height = 1;
        [enc setScissorRect:sc];
    }
}

void MetalRenderer::ApplyBlend(id<MTLRenderCommandEncoder> enc) {
    if (!tracker_) return;
    const auto& state = tracker_->GetState3D();
    const auto& blend = state.blend[0];
    if (blend.enabled) {
        [enc setBlendColorRed:state.blend_const[0]
                         green:state.blend_const[1]
                          blue:state.blend_const[2]
                         alpha:state.blend_const[3]];
    }
}

void MetalRenderer::ApplyDepthStencil(id<MTLRenderCommandEncoder> enc) {
    if (!tracker_) return;
    const auto& ds = tracker_->GetState3D().depth_stencil;

    MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
    desc.depthCompareFunction = ds.depth_enabled ? ToMetalCompare(ds.depth_func) : MTLCompareFunctionAlways;
    desc.depthWriteEnabled = ds.depth_write;

    if (ds.stencil_enable) {
        MTLStencilDescriptor* front = [[MTLStencilDescriptor alloc] init];
        front.stencilCompareFunction = ToMetalCompare(ds.stencil_front_func);
        front.stencilFailureOperation = ToMetalStencilOp(ds.stencil_front_fail);
        front.depthFailureOperation    = ToMetalStencilOp(ds.stencil_front_zfail);
        front.depthStencilPassOperation = ToMetalStencilOp(ds.stencil_front_zpass);
        front.readMask  = ds.stencil_front_mask;
        front.writeMask = ds.stencil_front_writemask;

        if (ds.stencil_two_side) {
            MTLStencilDescriptor* back = [[MTLStencilDescriptor alloc] init];
            back.stencilCompareFunction = ToMetalCompare(ds.stencil_back_func);
            back.stencilFailureOperation = ToMetalStencilOp(ds.stencil_back_fail);
            back.depthFailureOperation    = ToMetalStencilOp(ds.stencil_back_zfail);
            back.depthStencilPassOperation = ToMetalStencilOp(ds.stencil_back_zpass);
            back.readMask  = ds.stencil_back_mask;
            back.writeMask = ds.stencil_back_writemask;
            desc.backFaceStencil = back;
            [back release];
        } else {
            desc.backFaceStencil = front;
        }
        desc.frontFaceStencil = front;
        [front release];
    }

    id<MTLDepthStencilState> state = [device_.Device() newDepthStencilStateWithDescriptor:desc];
    [desc release];

    if (state) {
        [enc setDepthStencilState:state];
        if (ds.stencil_enable) {
            [enc setStencilFrontReferenceValue:ds.stencil_front_ref
                               backReferenceValue:(ds.stencil_two_side ? ds.stencil_back_ref : ds.stencil_front_ref)];
        }
        [state release];
    } else if (depth_stencil_) {
        [enc setDepthStencilState:depth_stencil_];
    }
}

void MetalRenderer::ApplyCulling(id<MTLRenderCommandEncoder> enc) {
    if (!tracker_) return;
    const auto& state = tracker_->GetState3D();
    if (state.cull_enable) {
        MTLCullMode mode = MTLCullModeNone;
        switch (state.cull_face) {
        case CullFace::Front:        mode = MTLCullModeFront; break;
        case CullFace::Back:         mode = MTLCullModeBack; break;
        case CullFace::FrontAndBack: mode = MTLCullModeFront; break;
        default: break;
        }
        [enc setCullMode:mode];
        MTLWinding winding = (state.front_face == FrontFace::CW) ? MTLWindingClockwise : MTLWindingCounterClockwise;
        [enc setFrontFacingWinding:winding];
    } else {
        [enc setCullMode:MTLCullModeNone];
    }
}

void MetalRenderer::BindVertexBuffers(id<MTLRenderCommandEncoder> enc, u32 count) {
    if (!tracker_) return;
    const auto& state = tracker_->GetState3D();
    auto* mem = tracker_->GetMemory();
    if (!mem) return;

    if (count == 0) count = 3;

    for (int i = 0; i < MAX_VERTEX_ARRAYS; i++) {
        const auto& va = state.vertex_arrays[i];
        if (va.enabled && va.address > 0 && va.stride > 0) {
            u32 buf_size = va.stride * count;
            if (buf_size == 0) buf_size = va.stride * 0x10000; // 64KB max for stride-only fallback
            id<MTLBuffer> buf = GetOrCreateCachedBuffer(va.address, buf_size);
            if (buf) {
                [enc setVertexBuffer:buf offset:0 atIndex:i];
            }
        }
    }
}

id<MTLBuffer> MetalRenderer::GetOrCreateCachedBuffer(u64 address, u32 min_size) {
    if (address == 0) return nil;

    // Check cache
    auto it = buffer_cache_.find(address);
    if (it != buffer_cache_.end()) {
        CachedBuffer& entry = it->second;
        // If existing buffer is large enough, reuse it
        if (entry.size >= min_size) {
            entry.last_frame_used = frame_count_;
            return entry.buffer;
        }
        // Buffer too small — release it and create a new one
        [entry.buffer release];
        buffer_cache_.erase(it);
    }

    // Create new no-copy wrapper around guest memory
    auto* mem = tracker_ ? tracker_->GetMemory() : nullptr;
    if (!mem) return nil;

    u8* guest_ptr = mem->Pointer(address);
    if (!guest_ptr) return nil;

    id<MTLBuffer> buf = [device_.Device() newBufferWithBytesNoCopy:guest_ptr
                                                            length:min_size
                                                           options:MTLResourceStorageModeShared
                                                       deallocator:nil];
    if (buf) {
        CachedBuffer entry;
        entry.buffer = buf;
        entry.last_frame_used = frame_count_;
        entry.size = (u32)[buf length];
        buffer_cache_[address] = entry;

        // Evict stale entries if cache exceeds threshold
        if (buffer_cache_.size() > BUFFER_CACHE_MAX_ENTRIES) {
            EvictLRU();
        }
    }
    return buf;
}

void MetalRenderer::InvalidateCachedBuffer(u64 address) {
    auto it = buffer_cache_.find(address);
    if (it != buffer_cache_.end()) {
        [it->second.buffer release];
        buffer_cache_.erase(it);
    }
}

void MetalRenderer::EvictLRU() {
    if (buffer_cache_.empty()) return;

    // Find the N least recently used entries
    // We do a single linear scan, collecting candidates by frame age
    const u32 to_evict = std::min(BUFFER_CACHE_EVICT_COUNT, (u32)buffer_cache_.size() - 1);

    // Sort by last_frame_used (oldest first)
    // Store sorted (address, frame) pairs to avoid repeated map lookups
    std::vector<std::pair<u64, u64>> sorted;
    sorted.reserve(buffer_cache_.size());
    for (const auto& [addr, entry] : buffer_cache_) {
        sorted.emplace_back(addr, entry.last_frame_used);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });

    // Evict the oldest entries
    for (u32 i = 0; i < to_evict; i++) {
        u64 addr = sorted[i].first;
        auto it = buffer_cache_.find(addr);
        if (it != buffer_cache_.end()) {
            [it->second.buffer release];
            buffer_cache_.erase(it);
        }
    }

    LOG_DEBUG("Buffer cache: evicted %u entries (%zu remaining)", to_evict, buffer_cache_.size());
}

void MetalRenderer::BindFragmentTextures(id<MTLRenderCommandEncoder> enc) {
    if (!has_game_textures_) return;

    for (u32 i = 0; i < MAX_BOUND_TEXTURES; i++) {
        if (game_textures_[i]) {
            [enc setFragmentTexture:game_textures_[i] atIndex:i];
        }
        if (game_samplers_[i]) {
            [enc setFragmentSamplerState:game_samplers_[i] atIndex:i];
        }
    }
}

void MetalRenderer::BindUniformBuffers(id<MTLRenderCommandEncoder> enc) {
    if (!tracker_) return;
    const auto& state = tracker_->GetState3D();
    auto* mem = tracker_->GetMemory();
    if (!mem || state.uniform_buffer_state == 0) return;

    id<MTLBuffer> ubo_buf = GetOrCreateCachedBuffer(state.uniform_buffer_state, 0x10000);
    if (ubo_buf) {
        [enc setVertexBuffer:ubo_buf offset:0 atIndex:30];
        [enc setFragmentBuffer:ubo_buf offset:0 atIndex:30];
    }
}

bool MetalRenderer::CompilePipeline() {
    if (!shader_mgr_ || !tracker_) return false;

    const auto& state = tracker_->GetState3D();
    if (state.program_region == 0) return false;

    // 检测着色器程序是否有变化
    if (shaders_compiled_ && last_program_region_ == state.program_region &&
        last_shader_offsets_[0] == state.shaders[0].offset &&
        last_shader_offsets_[1] == state.shaders[5].offset) {
        return true;  // 着色器未变化
    }

    auto* mem = tracker_->GetMemory();
    if (!mem) return false;

    u8* prog_ptr = mem->Pointer(state.program_region);
    if (!prog_ptr) return false;

    ShaderCompileResult vert;
    for (int i = 0; i < 2; i++) {
        if (state.shaders[i].enabled && state.shaders[i].offset > 0) {
            vert = shader_mgr_->CompileShader(prog_ptr, 16 * 1024 * 1024, state.shaders[i]);
            if (vert.success) break;
        }
    }

    ShaderCompileResult frag;
    if (state.shaders[5].enabled && state.shaders[5].offset > 0) {
        frag = shader_mgr_->CompileShader(prog_ptr, 16 * 1024 * 1024, state.shaders[5]);
    }

    if (vert.success && frag.success) {
        auto pipeline_result = shader_mgr_->CreateRenderPipeline(vert, frag);
        if (pipeline_result.success) {
            if (game_pipeline_) [game_pipeline_ release];
            game_pipeline_ = pipeline_result.pipeline_state;
            vert_result_ = vert;
            frag_result_ = frag;
            shaders_compiled_ = true;
            last_program_region_ = state.program_region;
            last_shader_offsets_[0] = state.shaders[0].offset;
            last_shader_offsets_[1] = state.shaders[5].offset;
            LOG_INFO("游戏着色器管线编译成功");
            return true;
        }
    }

    return false;
}

id<MTLRenderPipelineState> MetalRenderer::GetOrCreatePipeline(u64 hash) {
    auto it = pipeline_cache_.find(hash);
    if (it != pipeline_cache_.end()) return it->second;
    return nil;
}

id<MTLRenderPipelineState> MetalRenderer::CreatePipelineWithBlend(
    id<MTLFunction> vertFn, id<MTLFunction> fragFn,
    MTLPixelFormat colorFormat, MTLPixelFormat depthFormat,
    const BlendState& blend) {
    if (!vertFn || !fragFn) return nil;

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vertFn;
    desc.fragmentFunction = fragFn;

    // 色渲染目标
    desc.colorAttachments[0].pixelFormat = colorFormat;
    if (blend.enabled) {
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].rgbBlendOperation = ToMetalBlendOp(blend.color_op);
        desc.colorAttachments[0].sourceRGBBlendFactor = ToMetalBlendFactor(blend.src_color);
        desc.colorAttachments[0].destinationRGBBlendFactor = ToMetalBlendFactor(blend.dst_color);
        desc.colorAttachments[0].alphaBlendOperation = ToMetalBlendOp(blend.alpha_op);
        desc.colorAttachments[0].sourceAlphaBlendFactor = ToMetalBlendFactor(blend.src_alpha);
        desc.colorAttachments[0].destinationAlphaBlendFactor = ToMetalBlendFactor(blend.dst_alpha);
    } else {
        desc.colorAttachments[0].blendingEnabled = NO;
    }
    // 写入掩码 (RGBA 各占 1 bit)
    desc.colorAttachments[0].writeMask = (MTLColorWriteMask)(
        ((blend.color_mask & 1) ? MTLColorWriteMaskRed : 0) |
        ((blend.color_mask & 2) ? MTLColorWriteMaskGreen : 0) |
        ((blend.color_mask & 4) ? MTLColorWriteMaskBlue : 0) |
        ((blend.color_mask & 8) ? MTLColorWriteMaskAlpha : 0));

    // 深度目标
    if (depthFormat != MTLPixelFormatInvalid) {
        desc.depthAttachmentPixelFormat = depthFormat;
    }

    id<MTLRenderPipelineState> pipeline = device_.CreateRenderPipeline(desc);
    [desc release];
    return pipeline;
}

void MetalRenderer::IssueDraw(id<MTLRenderCommandEncoder> enc,
                                const StateTracker::PendingDraw& pending) {
    if (!tracker_) return;

    bool has_draw = (pending.arrays_count > 0) || (pending.index_count > 0);
    if (!has_draw) return;

    MTLPrimitiveType prim = ToMetalPrimitive(pending.primitive_type);

    if (pending.index_count > 0 && pending.index_addr > 0) {
        MTLIndexType idx_type = (pending.index_format == IndexFormat::Uint16)
            ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        u32 idx_size = (idx_type == MTLIndexTypeUInt16) ? 2 : 4;
        NSUInteger ibo_size = (NSUInteger)pending.index_count * idx_size;

        id<MTLBuffer> idx_buf = GetOrCreateCachedBuffer(pending.index_addr, (u32)ibo_size);
        if (idx_buf) {
            [enc drawIndexedPrimitives:prim
                             indexCount:pending.index_count
                              indexType:idx_type
                            indexBuffer:idx_buf
                      indexBufferOffset:0];
            LOG_INFO("Indexed draw: %u indices (batch key=0x%llx)",
                     pending.index_count, pending.pipeline_key);
        }
    } else if (pending.arrays_count > 0) {
        [enc drawPrimitives:prim
                vertexStart:pending.arrays_first
                vertexCount:pending.arrays_count];
        LOG_INFO("Array draw: %u vertices from %u (batch key=0x%llx)",
                 pending.arrays_count, pending.arrays_first, pending.pipeline_key);
    }
}

void MetalRenderer::IssueClear(id<MTLCommandBuffer> cmdBuf,
                                MTLRenderPassDescriptor* passDesc) {
    if (!tracker_) return;
    const auto& clear = tracker_->GetState3D().clear;
    if (clear.buffers == 0) return;

    if (clear.buffers & 1) {
        passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDesc.colorAttachments[0].clearColor =
            MTLClearColorMake(clear.color[0], clear.color[1], clear.color[2], clear.color[3]);
    }
    if (clear.buffers & 2) {
        passDesc.depthAttachment.loadAction = MTLLoadActionClear;
        passDesc.depthAttachment.clearDepth = clear.depth;
    }
    if (clear.buffers & 4) {
        passDesc.stencilAttachment.loadAction = MTLLoadActionClear;
        passDesc.stencilAttachment.clearStencil = clear.stencil;
    }
}

MTLPixelFormat MetalRenderer::RtFormatToMetal(RtFormat fmt) {
    switch (fmt) {
    case RtFormat::RGBA8Unorm:      return MTLPixelFormatRGBA8Unorm;
    case RtFormat::BGRA8Unorm_sRGB: return MTLPixelFormatBGRA8Unorm_sRGB;
    case RtFormat::RGBA16Float:     return MTLPixelFormatRGBA16Float;
    case RtFormat::RGBA32Float:     return MTLPixelFormatRGBA32Float;
    case RtFormat::R32Float:        return MTLPixelFormatR32Float;
    case RtFormat::R16Float:        return MTLPixelFormatR16Float;
    case RtFormat::R8Unorm:         return MTLPixelFormatR8Unorm;
    case RtFormat::RG8Unorm:        return MTLPixelFormatRG8Unorm;
    case RtFormat::RG16Float:       return MTLPixelFormatRG16Float;
    case RtFormat::RG32Float:       return MTLPixelFormatRG32Float;
    case RtFormat::RGBA16Unorm:     return MTLPixelFormatRGBA16Unorm;
    default:                        return MTLPixelFormatBGRA8Unorm_sRGB;
    }
}

u32 MetalRenderer::RtFormatBpp(RtFormat fmt) {
    switch (fmt) {
    case RtFormat::RGBA8Unorm:
    case RtFormat::BGRA8Unorm_sRGB:
        return 4;
    case RtFormat::RGBA16Float:
    case RtFormat::RGBA16Unorm:
    case RtFormat::RG32Float:
        return 8;
    case RtFormat::RGBA32Float:
        return 16;
    case RtFormat::R32Float:
        return 4;
    case RtFormat::R16Float:
        return 2;
    case RtFormat::R8Unorm:
        return 1;
    case RtFormat::RG8Unorm:
        return 2;
    case RtFormat::RG16Float:
        return 4;
    default:
        return 4;
    }
}

void MetalRenderer::ResolveRenderTargets() {
    if (!tracker_) return;

    const auto& state = tracker_->GetState3D();
    auto* mem = tracker_->GetMemory();

    rt_count_ = 0;
    rt_active_ = false;

    for (u32 i = 0; i < MAX_RT; i++) {
        const auto& rt = state.rt[i];
        if (rt.address == 0 || rt.width == 0 || rt.height == 0) {
            if (rt_textures_[i]) { [rt_textures_[i] release]; rt_textures_[i] = nil; }
            continue;
        }

        MTLPixelFormat mtl_fmt = RtFormatToMetal(rt.format);
        u32 rt_width = rt.width;
        u32 rt_height = rt.height;

        bool needs_recreate = !rt_textures_[i] ||
                               rt_textures_[i].width != rt_width ||
                               rt_textures_[i].height != rt_height ||
                               rt_textures_[i].pixelFormat != mtl_fmt;

        rt_textures_just_created_[i] = false;
        if (needs_recreate) {
            if (rt_textures_[i]) { [rt_textures_[i] release]; rt_textures_[i] = nil; }

            MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
            desc.textureType = MTLTextureType2D;
            desc.pixelFormat = mtl_fmt;
            desc.width = rt_width;
            desc.height = rt_height;
            desc.depth = 1;
            desc.mipmapLevelCount = 1;
            desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModePrivate;

            rt_textures_[i] = [device_.Device() newTextureWithDescriptor:desc];
            [desc release];

            if (rt_textures_[i]) {
                rt_textures_just_created_[i] = true;
                LOG_INFO("RT[%u]: %ux%u fmt=%u created at guest addr 0x%llx",
                         i, rt_width, rt_height, (u32)rt.format, rt.address);
            }
        }

        if (rt_textures_[i]) {
            rt_count_ = i + 1;
            rt_active_ = true;
        }
    }

    // Depth target
    const auto& dt = state.depth_target;
    if (dt.enabled && dt.address != 0 && dt.width > 0 && dt.height > 0) {
        MTLPixelFormat depth_fmt = MTLPixelFormatDepth32Float;
        if (dt.format == DepthFormat::Z16Unorm) depth_fmt = MTLPixelFormatDepth16Unorm;
        else if (dt.format == DepthFormat::Z24S8Unorm) depth_fmt = MTLPixelFormatDepth24Unorm_Stencil8;
        else if (dt.format == DepthFormat::Z32S8X24Float) depth_fmt = MTLPixelFormatDepth32Float_Stencil8;

        depth_just_created_ = false;
        bool needs_recreate = !depth_texture_ ||
                               depth_texture_.width != dt.width ||
                               depth_texture_.height != dt.height ||
                               depth_texture_.pixelFormat != depth_fmt;

        if (needs_recreate) {
            if (depth_texture_) { [depth_texture_ release]; depth_texture_ = nil; }

            MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
            desc.textureType = MTLTextureType2D;
            desc.pixelFormat = depth_fmt;
            desc.width = dt.width;
            desc.height = dt.height;
            desc.depth = 1;
            desc.mipmapLevelCount = 1;
            desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModePrivate;

            depth_texture_ = [device_.Device() newTextureWithDescriptor:desc];
            [desc release];

            if (depth_texture_) depth_just_created_ = true;
        }
    } else {
        if (depth_texture_) { [depth_texture_ release]; depth_texture_ = nil; }
    }
}

void MetalRenderer::UploadInitialRTData(id<MTLCommandBuffer> cmdBuf) {
    if (!tracker_ || !rt_active_) return;

    const auto& state = tracker_->GetState3D();
    auto* mem = tracker_->GetMemory();
    if (!mem) return;

    for (u32 i = 0; i < rt_count_; i++) {
        if (!rt_textures_just_created_[i]) continue;
        if (!rt_textures_[i]) continue;

        const auto& rt = state.rt[i];
        if (rt.address == 0) continue;

        u8* guest_ptr = mem->Pointer(rt.address);
        if (!guest_ptr) continue;

        u32 bpp = RtFormatBpp(rt.format);
        u32 width = rt.width;
        u32 height = rt.height;
        u32 stride = width * bpp;
        u32 size = stride * height;

        // Create staging buffer with initial guest data
        id<MTLBuffer> staging = [device_.Device() newBufferWithBytes:guest_ptr
                                                              length:size
                                                             options:MTLResourceStorageModeShared];
        if (!staging) continue;

        // Blit staging → private texture
        id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
        [blit copyFromBuffer:staging
                sourceOffset:0
           sourceBytesPerRow:stride
         sourceBytesPerImage:size
                  sourceSize:MTLSizeMake(width, height, 1)
                   toTexture:rt_textures_[i]
            destinationSlice:0
            destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];

        [staging release];

        LOG_TRACE("RT[%u]: uploaded initial data %ux%u stride=%u from guest 0x%llx",
                  i, width, height, stride, rt.address);
    }

    // Similarly for depth target
    if (depth_just_created_ && depth_texture_) {
        const auto& dt = state.depth_target;
        if (dt.address != 0) {
            u8* guest_ptr = mem->Pointer(dt.address);
            if (guest_ptr) {
                u32 stride = dt.width * 4; // Depth32Float = 4 bpp
                u32 size = stride * dt.height;

                id<MTLBuffer> staging = [device_.Device() newBufferWithBytes:guest_ptr
                                                                      length:size
                                                                     options:MTLResourceStorageModeShared];
                if (staging) {
                    id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
                    [blit copyFromBuffer:staging
                            sourceOffset:0
                       sourceBytesPerRow:stride
                     sourceBytesPerImage:size
                              sourceSize:MTLSizeMake(dt.width, dt.height, 1)
                               toTexture:depth_texture_
                        destinationSlice:0
                        destinationLevel:0
                       destinationOrigin:MTLOriginMake(0, 0, 0)];
                    [blit endEncoding];
                    [staging release];
                }
            }
        }
    }

    // Clear "just created" flags after upload
    for (u32 i = 0; i < MAX_RT; i++) rt_textures_just_created_[i] = false;
    depth_just_created_ = false;
}

void MetalRenderer::CopyRenderTargetsToGuest(id<MTLCommandBuffer> cmdBuf) {
    if (!tracker_ || !rt_active_) return;

    const auto& state = tracker_->GetState3D();
    auto* mem = tracker_->GetMemory();
    if (!mem) return;

    for (u32 i = 0; i < rt_count_; i++) {
        if (!rt_textures_[i]) continue;
        const auto& rt = state.rt[i];
        if (rt.address == 0) continue;

        u8* guest_ptr = mem->Pointer(rt.address);
        if (!guest_ptr) continue;

        u32 bpp = RtFormatBpp(rt.format);
        u32 width = rt_textures_[i].width;
        u32 height = rt_textures_[i].height;
        u32 stride = width * bpp;
        u32 size = stride * height;

        // Create a staging buffer for async blit copy from texture
        id<MTLBuffer> staging = [device_.Device() newBufferWithLength:size
                                                              options:MTLResourceStorageModeShared];
        if (!staging) continue;

        // Schedule async copy: texture → staging buffer
        id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
        [blit copyFromTexture:rt_textures_[i]
                   sourceSlice:0
                   sourceLevel:0
                  sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake(width, height, 1)
                      toBuffer:staging
             destinationOffset:0
        destinationBytesPerRow:stride
      destinationBytesPerImage:size];
        [blit endEncoding];

        // After GPU completes, copy staging → guest memory
        // and invalidate any texture cache entries overlapping this RT.
        __block u64 rt_addr = rt.address;
        __block u32 rt_size = size;
        __block TextureCache* tex_cache = texture_cache_;
        __block Memory* mem_ptr = mem;
        [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer>) {
            u8* staging_ptr = (u8*)[staging contents];
            if (staging_ptr && guest_ptr) {
                memcpy(guest_ptr, staging_ptr, size);
                // Invalidate stale texture cache entries overlapping this RT
                if (tex_cache) tex_cache->InvalidateRegion(rt_addr, rt_size);
            }
            [staging release];
        }];
    }
}

void MetalRenderer::SetFramebufferSource(const u8* guest_memory, u32 width,
                                          u32 height, u32 stride,
                                          u32 pixel_format) {
    if (!guest_memory || width == 0 || height == 0) {
        fb_active_ = false;
        return;
    }

    // Create or update framebuffer texture from guest memory
    if (!fb_active_ || fb_width_ != width || fb_height_ != height) {
        // New texture needed
        if (fb_texture_) [fb_texture_ release];

        MTLTextureDescriptor* desc = [[MTLTextureDescriptor alloc] init];
        desc.textureType = MTLTextureType2D;
        desc.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;  // VI always BGRA8
        desc.width = width;
        desc.height = height;
        desc.depth = 1;
        desc.mipmapLevelCount = 1;
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;

        fb_texture_ = [device_.Device() newTextureWithDescriptor:desc];
        [desc release];

        if (!fb_texture_) {
            LOG_WARN("Failed to create framebuffer texture");
            fb_active_ = false;
            return;
        }
        fb_width_ = width;
        fb_height_ = height;
        fb_stride_ = stride;
    }

    // Upload framebuffer data (using no-copy is tricky since guest memory changes)
    // Instead, we use replaceRegion each frame
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [fb_texture_ replaceRegion:region
                   mipmapLevel:0
                     withBytes:guest_memory
                   bytesPerRow:stride];
    fb_active_ = true;
}

void MetalRenderer::BindGameTextures() {
    if (!tracker_ || !texture_cache_) return;
    const auto& state = tracker_->GetState3D();
    auto* mem = tracker_->GetMemory();
    if (!mem || state.tex_header_pool == 0 || state.tex_header_max_idx == 0) return;

    u8* tex_pool = mem->Pointer(state.tex_header_pool);
    u8* sampler_pool = (state.tex_sampler_pool != 0) ? mem->Pointer(state.tex_sampler_pool) : nullptr;
    if (!tex_pool) return;

    for (u32 i = 0; i < state.tex_header_max_idx && i < MAX_BOUND_TEXTURES; i++) {
        TextureInfo info = texture_cache_->ParseTIC(tex_pool, i * 64);
        if (info.gpu_address == 0 || info.width == 0 || info.height == 0) continue;

        u8* guest_tex = mem->Pointer(info.gpu_address);
        if (!guest_tex) continue;

        id<MTLTexture> tex = texture_cache_->GetOrCreate(info, guest_tex);
        if (tex) SetGameTexture(i, tex);

        // ── Bind sampler from TSC pool ──────────────────
        // TSC entries are 32 bytes each (8 words × 4 bytes), indexed
        // in parallel with TIC entries. Border color floats are at offset 8.
        if (sampler_pool && i < state.tex_sampler_max_idx) {
            SamplerInfo samp_info = texture_cache_->ParseTSC(sampler_pool, (u64)i * 32);
            id<MTLSamplerState> sampler = texture_cache_->GetOrCreateSampler(samp_info);
            if (game_samplers_[i] != sampler) {
                if (game_samplers_[i]) [game_samplers_[i] release];
                game_samplers_[i] = sampler;
                if (sampler) [sampler retain];
            }
        }
    }
}

void MetalRenderer::RenderFrame(id<MTLCommandBuffer> cmdBuf,
                                 MTLRenderPassDescriptor* passDesc) {
    if (!initialized_ || !passDesc) return;

    frame_count_++;

    // ── Consume draw queue ──────────────────────────────
    bool has_3d_draws = (tracker_ && tracker_->HasPendingDraws());

    if (has_3d_draws) {
        // Pre-frame setup: textures, render targets, initial upload (once per frame)
        BindGameTextures();
        ResolveRenderTargets();
        UploadInitialRTData(cmdBuf);

        bool is_rt_path = (rt_active_ && rt_textures_[0]);

        // Consume the entire draw queue for batch processing
        auto draws = tracker_->ConsumeDraws();

        if (!draws.empty()) {
            id<MTLRenderCommandEncoder> batch_enc = nil;
            MTLRenderPassDescriptor* rpDesc = nil;
            u64 last_pipeline_key = 0;

            for (u32 di = 0; di < draws.size(); di++) {
                const auto& draw = draws[di];

                // ── Batch split: new encoder when pipeline/RT key changes ──
                // For direct-to-screen path, never split (drawable is consumed once).
                // For RT path, split on pipeline_key changes.
                bool key_changed = (draw.pipeline_key != last_pipeline_key);
                bool new_batch = (di == 0) || (is_rt_path && key_changed);

                if (new_batch) {
                    // End previous batch encoder
                    if (batch_enc) {
                        [batch_enc endEncoding];
                        [rpDesc release];
                        batch_enc = nil;
                        rpDesc = nil;
                    }

                    // Create render pass descriptor for this batch
                    if (is_rt_path) {
                        rpDesc = [[MTLRenderPassDescriptor alloc] init];
                        for (u32 i = 0; i < rt_count_; i++) {
                            if (rt_textures_[i]) {
                                rpDesc.colorAttachments[i].texture = rt_textures_[i];
                                rpDesc.colorAttachments[i].storeAction = MTLStoreActionStore;
                            }
                        }
                        IssueClear(cmdBuf, rpDesc);
                        if (depth_texture_) {
                            rpDesc.depthAttachment.texture = depth_texture_;
                            rpDesc.depthAttachment.storeAction = MTLStoreActionStore;
                        }
                    } else {
                        rpDesc = [passDesc retain];
                        IssueClear(cmdBuf, rpDesc);
                    }

                    // Create encoder for this batch
                    batch_enc = [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];

                    // Compile shader if needed, then look up or create blend-specific pipeline
                    CompilePipeline();

                    id<MTLRenderPipelineState> pipeline = nil;
                    // Check pipeline cache for existing blend-configured pipeline
                    pipeline = GetOrCreatePipeline(draw.pipeline_key);

                    if (!pipeline) {
                        // Create a new pipeline with current blend state
                        MTLPixelFormat color_fmt = is_rt_path
                            ? RtFormatToMetal(tracker_->GetState3D().rt[0].format)
                            : MTLPixelFormatBGRA8Unorm_sRGB;
                        MTLPixelFormat depth_fmt = depth_texture_
                            ? depth_texture_.pixelFormat : MTLPixelFormatInvalid;

                        if (vert_result_.success && frag_result_.success) {
                            pipeline = CreatePipelineWithBlend(
                                (id<MTLFunction>)vert_result_.vertex_function,
                                (id<MTLFunction>)frag_result_.fragment_function,
                                color_fmt, depth_fmt,
                                tracker_->GetState3D().blend[0]);
                        }

                        if (pipeline) {
                            pipeline_cache_[draw.pipeline_key] = pipeline;
                        }
                    }

                    if (!pipeline) {
                        pipeline = game_pipeline_ ? game_pipeline_ : fallback_pipeline_;
                    }

                    [batch_enc setRenderPipelineState:pipeline];

                    // Apply batch-level state (textures, uniforms)
                    BindFragmentTextures(batch_enc);
                    BindUniformBuffers(batch_enc);
                }

                // ── Per-draw state ──────────────────────
                ApplyViewport(batch_enc);
                ApplyScissor(batch_enc);
                ApplyBlend(batch_enc);
                ApplyDepthStencil(batch_enc);
                ApplyCulling(batch_enc);

                // ── Per-draw bindings ──────────────────
                u32 vertex_count = std::max(draw.arrays_count, draw.index_count);
                BindVertexBuffers(batch_enc, vertex_count);

                // ── Issue draw call ────────────────────
                IssueDraw(batch_enc, draw);

                last_pipeline_key = draw.pipeline_key;
            }

            // End final batch encoder
            if (batch_enc) {
                [batch_enc endEncoding];
                [rpDesc release];
            }

            // ── Post-frame: blit RT[0] to screen + copy back to guest ──
            if (is_rt_path && rt_textures_[0] &&
                passDesc.colorAttachments[0].texture) {
                id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
                [blit copyFromTexture:rt_textures_[0]
                           sourceSlice:0
                           sourceLevel:0
                          sourceOrigin:MTLOriginMake(0, 0, 0)
                            sourceSize:MTLSizeMake(
                                rt_textures_[0].width,
                                rt_textures_[0].height, 1)
                             toTexture:passDesc.colorAttachments[0].texture
                      destinationSlice:0
                      destinationLevel:0
                     destinationOrigin:MTLOriginMake(0, 0, 0)];
                [blit endEncoding];
                CopyRenderTargetsToGuest(cmdBuf);
            }
        }

        // Reset per-frame state
        if (tracker_) {
            tracker_->GetState3D().clear.buffers = 0;
            tracker_->ClearDirty();
        }
    } else if (fb_active_ && fb_texture_ && fb_pipeline_) {
        // ── VI framebuffer passthrough (no 3D draws) ──
        id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:passDesc];
        [enc setRenderPipelineState:fb_pipeline_];
        [enc setFragmentTexture:fb_texture_ atIndex:0];
        [enc setFragmentSamplerState:fb_sampler_ atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [enc endEncoding];
    } else if (vertex_buffer_) {
        // ── Fallback test triangle ─────────────────────
        id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:passDesc];
        [enc setRenderPipelineState:fallback_pipeline_];
        [enc setVertexBuffer:vertex_buffer_ offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
    }
}

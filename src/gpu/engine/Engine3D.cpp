#include "gpu/engine/Engine3D.h"
#include "common/Log.h"
#include <cstring>

Engine3D::Engine3D() { Reset(); }

void Engine3D::Reset() {
    std::memset(&state_, 0, sizeof(state_));
    state_.rasterizer_enable = true;
    state_.polygon_mode_front = PolygonMode::Fill;
    state_.polygon_mode_back  = PolygonMode::Fill;
    state_.front_face  = FrontFace::CCW;
    state_.cull_face   = CullFace::Back;
    state_.depth_stencil.depth_func = CompareOp::Less;
    state_.depth_stencil.depth_write = true;
    state_.multisample.sample_mask = 0xFFFFFFFF;
}

u32 Engine3D::GetRegister(u32 method) const {
    // Phase 2: stub — read will be implemented for complex methods
    LOG_WARN("GetRegister(0x%x) not fully implemented", method);
    return 0;
}

void Engine3D::HandleMethod(u32 method, u32 value, bool& is_draw) {
    is_draw = false;

    // Quick dispatch table for common methods
    switch (static_cast<Method3D>(method)) {

    // ── Render targets ─────────────────────────────
    case Method3D::RenderTarget:
        HandleRenderTarget(0, value);
        break;

    case Method3D::RenderTargetControl:
        state_.rt_control = value;
        break;

    // ── Viewports ─────────────────────────────────
    case Method3D::ViewportTransform:
        HandleViewport(0, value);
        break;

    case Method3D::Viewport:
        HandleViewport(0, value);  // Viewport array starts at 0x300
        break;

    // ── Scissors ──────────────────────────────────
    case Method3D::Scissor:
        HandleScissor(0, value);
        break;

    case Method3D::ScreenScissorHorizontal:
        LOG_TRACE("ScreenScissorH=0x%x", value);
        break;

    case Method3D::ScreenScissorVertical:
        LOG_TRACE("ScreenScissorV=0x%x", value);
        break;

    // ── Draw commands ─────────────────────────────
    case Method3D::DrawArraysFirst:
        state_.draw_arrays_first = value;
        break;

    case Method3D::DrawArraysCount:
        state_.draw_arrays_count = value;
        is_draw = true;  // Triggers a non-indexed draw
        break;

    case Method3D::DrawElementsFirst:
        state_.draw_elements_first = value;
        break;

    case Method3D::DrawElementsCount:
        state_.draw_elements_count = value;
        is_draw = true;  // Triggers an indexed draw
        break;

    // ── Vertex arrays ─────────────────────────────
    case Method3D::VertexArray:
        HandleVertexArray(0, value);
        break;

    case Method3D::VertexArrayLimit:
        // VBO end address
        break;

    case Method3D::VertexAttribState:
        HandleVertexAttrib(0, value);
        break;

    case Method3D::IsVertexArrayPerInstance:
        LOG_TRACE("VtxInstanced: 0x%x", value);
        break;

    // ── Index buffer ──────────────────────────────
    case Method3D::IndexArrayStartIova:
        state_.index_buffer.address = (state_.index_buffer.address & ~0xFFFFFFFFULL) | value;
        break;

    case Method3D::IndexArrayFormat:
        state_.index_buffer.format = static_cast<IndexFormat>(value);
        break;

    // ── Shaders ───────────────────────────────────
    case Method3D::SetProgram:
        HandleShader(0, value);
        break;

    case Method3D::SetProgramRegion:
        state_.program_region = (state_.program_region & ~0xFFFFFFFFULL) | value;
        break;

    case Method3D::SetTexHeaderPool:
        state_.tex_header_pool = (state_.tex_header_pool & ~0xFFFFFFFFULL) | value;
        break;

    case Method3D::SetTexHeaderPoolMaximumIndex:
        state_.tex_header_max_idx = value;
        break;

    case Method3D::SetTexSamplerPool:
        state_.tex_sampler_pool = (state_.tex_sampler_pool & ~0xFFFFFFFFULL) | value;
        break;

    case Method3D::SetTexSamplerPoolMaximumIndex:
        state_.tex_sampler_max_idx = value;
        break;

    // ── Depth/Stencil ─────────────────────────────
    case Method3D::DepthTargetAddr:
        state_.depth_target.address = (state_.depth_target.address & ~0xFFFFFFFFULL) | value;
        break;

    case Method3D::DepthTargetFormat:
        state_.depth_target.format = static_cast<DepthFormat>(value);
        break;

    case Method3D::DepthTargetEnable:
        state_.depth_target.enabled = (value != 0);
        break;

    case Method3D::DepthTestEnable:
        state_.depth_stencil.depth_enabled = (value != 0);
        break;

    case Method3D::DepthWriteEnable:
        state_.depth_stencil.depth_write = (value != 0);
        break;

    case Method3D::DepthTestFunc:
        state_.depth_stencil.depth_func = static_cast<CompareOp>(value);
        break;

    case Method3D::StencilEnable:
        state_.depth_stencil.stencil_enable = (value != 0);
        break;

    case Method3D::StencilFrontFunc:
        state_.depth_stencil.stencil_front_func = static_cast<CompareOp>(value & 0xF);
        state_.depth_stencil.stencil_front_ref = (value >> 16) & 0xFF;
        break;

    // ── Blend ─────────────────────────────────────
    case Method3D::IndependentBlend:
        HandleBlend(0, value);
        break;

    case Method3D::IndependentBlendEnable:
        state_.independent_blend = (value != 0);
        break;

    case Method3D::ColorBlendEnable:
        state_.blend[0].enabled = (value != 0);
        break;

    case Method3D::BlendConstant:
        std::memcpy(state_.blend_const, &value, 4);
        break;

    case Method3D::ColorWriteMask:
        state_.blend[0].color_mask = value & 0xF;
        break;

    // ── Rasterization ─────────────────────────────
    case Method3D::RasterizerEnable:
        state_.rasterizer_enable = (value != 0);
        break;

    case Method3D::SetFrontFace:
        state_.front_face = static_cast<FrontFace>(value);
        break;

    case Method3D::SetCullFace:
        state_.cull_face = static_cast<CullFace>(value);
        break;

    case Method3D::CullFaceEnable:
        state_.cull_enable = (value != 0);
        break;

    case Method3D::SetPolygonModeFront:
        state_.polygon_mode_front = static_cast<PolygonMode>(value);
        break;

    case Method3D::SetPolygonModeBack:
        state_.polygon_mode_back = static_cast<PolygonMode>(value);
        break;

    case Method3D::VertexBeginGl:
        state_.primitive_type = static_cast<PrimitiveType>(value & 0xF);
        break;

    // ── Clear ─────────────────────────────────────
    case Method3D::ClearColor:
        std::memcpy(state_.clear.color, &value, 4);
        break;

    case Method3D::ClearDepth:
        state_.clear.depth = *reinterpret_cast<const f32*>(&value);
        break;

    case Method3D::ClearStencil:
        state_.clear.stencil = value;
        break;

    case Method3D::ClearBuffers:
        state_.clear_buffers_flags = value;
        // Triggers a clear operation (handled in Metal backend)
        break;

    // ── Misc ──────────────────────────────────────
    case Method3D::MultisampleEnable:
        state_.multisample.enable = (value != 0);
        break;

    case Method3D::MultisampleControl:
        state_.multisample.alpha_to_coverage = (value & 1) != 0;
        state_.multisample.alpha_to_one = ((value >> 4) & 1) != 0;
        break;

    case Method3D::ViewportTransformEnable:
        state_.viewport_transform_enable = (value != 0);
        break;

    case Method3D::ViewVolumeClipControl:
        state_.depth_clamp = ((value >> 3) & 1) != 0;
        break;

    case Method3D::PrimitiveRestartEnable:
        state_.primitive_restart = (value != 0);
        break;

    case Method3D::PrimitiveRestartIndex:
        state_.primitive_restart_index = value;
        break;

    // ── Ignore / no-op ────────────────────────────
    case Method3D::NoOperation:
    case Method3D::PipeNop:
    case Method3D::WaitForIdle:
    case Method3D::InvalidateShaderCaches:
    case Method3D::InvalidateTextureDataCache:
    case Method3D::InvalidateTextureDataCacheNoWfi:
    case Method3D::FragmentBarrier:
        // No state change needed
        break;

    default:
        // Log unknown methods at TRACE level (very noisy!)
        LOG_TRACE("Unhandled 3D method 0x%x (value=0x%08x)", method, value);
        break;
    }
}

// ── Method group handlers ──────────────────────────────────
// These handle array-indexed methods by computing the index
// from the method offset within the array.

void Engine3D::HandleRenderTarget(u32 index, u32 value) {
    // RenderTarget registers are at 0x200 + i * 0x10
    // Each RT has: Addr, H, V, Format, TileMode, ArrayMode, LayerStride, BaseLayer
    u32 i = index % 8;
    u32 sub = index / 8;  // Sub-register within the RT entry
    switch (sub) {
    case 0: state_.rt[i].address = (state_.rt[i].address & ~0xFFFFFFFFULL) | value; break;
    case 1: break; // Horizontal (not stored directly)
    case 2: break; // Vertical
    case 3: state_.rt[i].format = static_cast<RtFormat>(value); break;
    default: break;
    }
}

void Engine3D::HandleViewport(u32 index, u32 value) {
    u32 i = index % 16;
    LOG_TRACE("Viewport[%u] = 0x%x", i, value);
}

void Engine3D::HandleScissor(u32 index, u32 value) {
    u32 i = index % 16;
    if (i < MAX_SCISSORS) {
        if (index / 16 == 0) {
            state_.scissors[i].enabled = (value & 1) != 0;
        }
    }
}

void Engine3D::HandleVertexArray(u32 index, u32 value) {
    // VertexArray[0].Config, [0].Start low, [0].Start high, [0].Divisor, [1].Config...
    u32 i = (index / 4) % MAX_VERTEX_ARRAYS;
    u32 sub = index % 4;
    if (i < MAX_VERTEX_ARRAYS) {
        switch (sub) {
        case 0: // Config: stride(0..11), enable(12)
            state_.vertex_arrays[i].stride  = value & 0xFFF;
            state_.vertex_arrays[i].enabled = (value >> 12) & 1;
            break;
        case 1: // Start low (address low 32 bits)
            state_.vertex_arrays[i].address = (state_.vertex_arrays[i].address & ~0xFFFFFFFFULL) | value;
            break;
        case 3: // Divisor
            state_.vertex_arrays[i].divisor = value;
            break;
        }
    }
}

void Engine3D::HandleVertexAttrib(u32 index, u32 value) {
    u32 i = index % MAX_VERTEX_ATTRIBS;
    if (i < MAX_VERTEX_ATTRIBS) {
        state_.vertex_attribs[i].buffer_index = value & 0x1F;
        state_.vertex_attribs[i].is_fixed  = (value >> 6) & 1;
        state_.vertex_attribs[i].offset    = (value >> 7) & 0x3FFF;
        state_.vertex_attribs[i].size      = (value >> 21) & 0x3F;
        state_.vertex_attribs[i].type      = (value >> 27) & 0x7;
        state_.vertex_attribs[i].is_bgra   = (value >> 31) & 1;
    }
}

void Engine3D::HandleBlend(u32 index, u32 value) {
    u32 i = index % MAX_BLEND_TARGETS;
    if (i < MAX_BLEND_TARGETS) {
        // blend[0].EquationRgb, .FuncRgbSrc, .FuncRgbDst, .EquationAlpha, .FuncAlphaSrc, .FuncAlphaDst
        u32 sub = index / MAX_BLEND_TARGETS;
        switch (sub) {
        case 1: state_.blend[i].color_op = static_cast<BlendOp>(value); break;
        case 2: state_.blend[i].src_color = static_cast<BlendFactor>(value); break;
        case 3: state_.blend[i].dst_color = static_cast<BlendFactor>(value); break;
        case 4: state_.blend[i].alpha_op = static_cast<BlendOp>(value); break;
        case 5: state_.blend[i].src_alpha = static_cast<BlendFactor>(value); break;
        case 6: state_.blend[i].dst_alpha = static_cast<BlendFactor>(value); break;
        }
    }
}

void Engine3D::HandleShader(u32 index, u32 value) {
    u32 i = index % MAX_SHADER_STAGES;
    if (i < MAX_SHADER_STAGES) {
        // shader[0].Config (enable + stage), [0+1].Offset
        u32 sub = index / MAX_SHADER_STAGES;
        switch (sub) {
        case 0: // Config
            state_.shaders[i].enabled  = (value & 1) != 0;
            state_.shaders[i].stage_id = (value >> 4) & 0x7;
            break;
        case 1: // Offset (low 32 bits)
            state_.shaders[i].offset = (state_.shaders[i].offset & ~0xFFFFFFFFULL) | value;
            break;
        }
    }
}

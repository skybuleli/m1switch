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

    // ── 数组方法范围路由 ──────────────────────────────────────
    // GPFifo Increasing 模式会发送 method, method+1, method+2 ...
    // 这些不能被 switch 枚举匹配，需要范围检查先行路由。

    // RenderTarget: 0x200 + i*8 (i=0..7), 每组 8 个子寄存器
    if (method >= 0x200 && method < 0x200 + 8*8) {
        u32 offset = method - 0x200;
        HandleRenderTarget(offset / 8, value);
        return;
    }
    // ViewportTransform: 0x280 + i (i=0..15*6=95)
    if (method >= 0x280 && method < 0x280 + 16*6) {
        HandleViewport(method - 0x280, value);
        return;
    }
    // Scissor: 0x380 + i (i=0..15)
    if (method >= static_cast<u32>(Method3D::Scissor) &&
        method < static_cast<u32>(Method3D::Scissor) + 16) {
        HandleScissor(method - static_cast<u32>(Method3D::Scissor), value);
        return;
    }
    // VertexAttrib: 0x458 + i (i=0..31)
    if (method >= static_cast<u32>(Method3D::VertexAttribState) &&
        method < static_cast<u32>(Method3D::VertexAttribState) + 32) {
        HandleVertexAttrib(method - static_cast<u32>(Method3D::VertexAttribState), value);
        return;
    }
    // ColorBlendEnable: 0x4D8 + i (i=0..7)
    if (method >= static_cast<u32>(Method3D::ColorBlendEnable) &&
        method < static_cast<u32>(Method3D::ColorBlendEnable) + 8) {
        u32 i = method - static_cast<u32>(Method3D::ColorBlendEnable);
        if (i < MAX_BLEND_TARGETS) {
            state_.blend[i].enabled = (value != 0);
        }
        return;
    }
    // StencilFrontFuncRef: 0x4E5
    if (method == static_cast<u32>(Method3D::StencilFrontFuncRef)) {
        state_.depth_stencil.stencil_front_ref = value & 0xFF;
        return;
    }
    // StencilFrontFuncMask: 0x4E6
    if (method == static_cast<u32>(Method3D::StencilFrontFuncMask)) {
        state_.depth_stencil.stencil_front_mask = value & 0xFF;
        return;
    }
    // StencilFrontMask: 0x4E7 (write mask)
    if (method == static_cast<u32>(Method3D::StencilFrontMask)) {
        state_.depth_stencil.stencil_front_writemask = value & 0xFF;
        return;
    }
    // ColorWriteMask: 0x680 + i (i=0..7)
    if (method >= static_cast<u32>(Method3D::ColorWriteMask) &&
        method < static_cast<u32>(Method3D::ColorWriteMask) + 8) {
        u32 i = method - static_cast<u32>(Method3D::ColorWriteMask);
        if (i < MAX_BLEND_TARGETS) {
            state_.blend[i].color_mask = value & 0xF;
        }
        return;
    }
    // VertexArray: 0x700 + i*4 + sub (i=0..15)
    if (method >= static_cast<u32>(Method3D::VertexArray) &&
        method < static_cast<u32>(Method3D::VertexArray) + 16*4) {
        HandleVertexArray(method - static_cast<u32>(Method3D::VertexArray), value);
        return;
    }
    // IndependentBlend: 0x780 + i (i=0..7, 6 个子寄存器)
    if (method >= static_cast<u32>(Method3D::IndependentBlend) &&
        method < static_cast<u32>(Method3D::IndependentBlend) + 8*7) {
        HandleBlend(method - static_cast<u32>(Method3D::IndependentBlend), value);
        return;
    }
    // VertexArrayLimit: 0x7C0 + i (i=0..15)
    if (method >= static_cast<u32>(Method3D::VertexArrayLimit) &&
        method < static_cast<u32>(Method3D::VertexArrayLimit) + 16) {
        LOG_TRACE("VertexArrayLimit[%u] = 0x%x", method - static_cast<u32>(Method3D::VertexArrayLimit), value);
        return;
    }
    // SetProgram: 0x800 + i (shader stages)
    if (method >= static_cast<u32>(Method3D::SetProgram) &&
        method < static_cast<u32>(Method3D::SetProgram) + 6*2) {
        HandleShader(method - static_cast<u32>(Method3D::SetProgram), value);
        return;
    }
    // IsVertexArrayPerInstance: 0x620 + i (i=0..15)
    if (method >= static_cast<u32>(Method3D::IsVertexArrayPerInstance) &&
        method < static_cast<u32>(Method3D::IsVertexArrayPerInstance) + 16) {
        u32 i = method - static_cast<u32>(Method3D::IsVertexArrayPerInstance);
        if (i < MAX_VERTEX_ARRAYS) {
            state_.vertex_arrays[i].divisor = (value != 0) ? 1 : 0;
        }
        return;
    }

    // ── 非数组方法：switch 分发 ────────────────────────────────
    switch (static_cast<Method3D>(method)) {

    case Method3D::RenderTargetControl:
        state_.rt_control = value;
        break;

    // ── Draw commands ─────────────────────────────
    case Method3D::DrawArraysFirst:
        state_.draw_arrays_first = value;
        break;

    case Method3D::DrawArraysCount:
        state_.draw_arrays_count = value;
        is_draw = true;
        break;

    case Method3D::DrawElementsFirst:
        state_.draw_elements_first = value;
        break;

    case Method3D::DrawElementsCount:
        state_.draw_elements_count = value;
        is_draw = true;
        break;

    case Method3D::ScreenScissorHorizontal:
        LOG_TRACE("ScreenScissorH=0x%x", value);
        break;

    case Method3D::ScreenScissorVertical:
        LOG_TRACE("ScreenScissorV=0x%x", value);
        break;

    // ── Index buffer ──────────────────────────────
    case Method3D::IndexArrayStartIova:
        state_.index_buffer.address = (state_.index_buffer.address & ~0xFFFFFFFFULL) | value;
        break;

    case static_cast<Method3D>(0x5F3):  // IndexArrayStartIova 高32位
        state_.index_buffer.address = (state_.index_buffer.address & 0xFFFFFFFFULL) | ((u64)value << 32);
        break;

    case Method3D::IndexArrayLimitIova:
        state_.index_buffer.limit = (state_.index_buffer.limit & ~0xFFFFFFFFULL) | value;
        break;

    case static_cast<Method3D>(0x5F5):  // IndexArrayLimitIova 高32位
        state_.index_buffer.limit = (state_.index_buffer.limit & 0xFFFFFFFFULL) | ((u64)value << 32);
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

    case static_cast<Method3D>(0x583):  // SetProgramRegion 高32位
        state_.program_region = (state_.program_region & 0xFFFFFFFFULL) | ((u64)value << 32);
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

    case Method3D::DepthTargetHorizontal:
        state_.depth_target.width = value;
        break;

    case Method3D::DepthTargetVertical:
        state_.depth_target.height = value;
        break;

    case Method3D::DepthTargetArrayMode:
        state_.depth_target.tile_mode = value;
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

    case Method3D::StencilFrontOpFail:
        state_.depth_stencil.stencil_front_fail = static_cast<StencilOp>(value & 0xF);
        break;

    case Method3D::StencilFrontOpZFail:
        state_.depth_stencil.stencil_front_zfail = static_cast<StencilOp>(value & 0xF);
        break;

    case Method3D::StencilFrontOpZPass:
        state_.depth_stencil.stencil_front_zpass = static_cast<StencilOp>(value & 0xF);
        break;

    case Method3D::StencilTwoSideEnable:
        state_.depth_stencil.stencil_two_side = (value != 0);
        break;

    case Method3D::StencilBackFunc:
        state_.depth_stencil.stencil_back_func = static_cast<CompareOp>(value & 0xF);
        break;

    case Method3D::StencilBackFuncRef:
        state_.depth_stencil.stencil_back_ref = value & 0xFF;
        break;

    case Method3D::StencilBackMask:
        state_.depth_stencil.stencil_back_mask = value & 0xFF;
        break;

    case Method3D::StencilBackOpFail:
        state_.depth_stencil.stencil_back_fail = static_cast<StencilOp>(value & 0xF);
        break;

    case Method3D::StencilBackOpZFail:
        state_.depth_stencil.stencil_back_zfail = static_cast<StencilOp>(value & 0xF);
        break;

    case Method3D::StencilBackOpZPass:
        state_.depth_stencil.stencil_back_zpass = static_cast<StencilOp>(value & 0xF);
        break;

    case Method3D::DepthBoundsEnable:
        state_.depth_stencil.depth_bounds_enable = (value != 0);
        break;

    case Method3D::DepthBoundsNear:
        state_.depth_stencil.depth_bounds_near = *reinterpret_cast<const f32*>(&value);
        break;

    case Method3D::DepthBoundsFar:
        state_.depth_stencil.depth_bounds_far = *reinterpret_cast<const f32*>(&value);
        break;

    // ── Blend ─────────────────────────────────────
    // 注意: ColorBlendEnable, IndependentBlend, ColorWriteMask
    // 已经在数组预路由中处理，此处不再重复。

    case Method3D::IndependentBlendEnable:
        state_.independent_blend = (value != 0);
        break;

    case Method3D::BlendConstant:
        std::memcpy(state_.blend_const, &value, 4);
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

    case Method3D::PolygonOffsetPointEnable:
        state_.polygon_offset_point = (value != 0);
        break;

    case Method3D::PolygonOffsetLineEnable:
        state_.polygon_offset_line = (value != 0);
        break;

    case Method3D::PolygonOffsetFillEnable:
        state_.polygon_offset_fill = (value != 0);
        break;

    case Method3D::PolygonOffsetFactor:
        state_.polygon_offset_factor = *reinterpret_cast<const f32*>(&value);
        break;

    case Method3D::PolygonOffsetUnits:
        state_.polygon_offset_units = *reinterpret_cast<const f32*>(&value);
        break;

    case Method3D::PolygonOffsetClamp:
        state_.polygon_offset_clamp = *reinterpret_cast<const f32*>(&value);
        break;

    case Method3D::AlphaTestEnable:
        state_.alpha_test_enable = (value != 0);
        break;

    case Method3D::AlphaTestRef:
        state_.alpha_test_ref = *reinterpret_cast<const f32*>(&value);
        break;

    case Method3D::AlphaTestFunc:
        state_.alpha_test_func = static_cast<CompareOp>(value);
        break;

    case Method3D::ColorLogicOpEnable:
        state_.logic_op_enable = (value != 0);
        break;

    case Method3D::ColorLogicOpType:
        state_.logic_op = static_cast<LogicOp>(value);
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
        state_.clear.buffers = value;
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
    // RenderTarget registers are at 0x200 + i * 0x10 (i=0..7)
    // Each RT entry has 8 sub-registers:
    //   0: Address (low 32 bits), 1: Width (H), 2: Height (V),
    //   3: Format, 4: TileMode, 5: ArrayMode, 6: LayerStride, 7: BaseLayer
    u32 i = index % 8;
    u32 sub = index / 8;
    switch (sub) {
    case 0: state_.rt[i].address   = (state_.rt[i].address & ~0xFFFFFFFFULL) | value; break;
    case 1: state_.rt[i].width     = value; break;
    case 2: state_.rt[i].height    = value; break;
    case 3: state_.rt[i].format    = static_cast<RtFormat>(value); break;
    case 4: state_.rt[i].tile_mode = value; break;
    case 5: state_.rt[i].array_mode = value; break;
    case 6: state_.rt[i].layer_stride = value; break;
    case 7: state_.rt[i].base_layer = value; break;
    default: break;
    }
}

void Engine3D::HandleViewport(u32 index, u32 value) {
    // ViewportTransform: 0x280 + 16组 × 6个f32寄存器
    // 每组 viewport 有: scale_x(0), scale_y(1), scale_z(2),
    //                    translate_x(3), translate_y(4), translate_z(5), swizzle(6+)
    // Linux 内核 Tegra X1: 每组 6-8 个寄存器
    u32 vp_index = index / 8;   // viewport 序号 (0..15)
    u32 sub = index % 8;         // 子寄存器偏移

    if (vp_index >= MAX_VIEWPORTS) {
        LOG_WARN("Viewport[%u] 超出范围 (index=%u)", vp_index, index);
        return;
    }

    // value 是 u32, 需要重新解释为 f32
    f32 fval;
    std::memcpy(&fval, &value, sizeof(fval));

    switch (sub) {
    case 0: state_.viewports[vp_index].scale_x = fval; break;
    case 1: state_.viewports[vp_index].scale_y = fval; break;
    case 2: state_.viewports[vp_index].scale_z = fval; break;
    case 3: state_.viewports[vp_index].translate_x = fval; break;
    case 4: state_.viewports[vp_index].translate_y = fval; break;
    case 5: state_.viewports[vp_index].translate_z = fval; break;
    case 6: state_.viewports[vp_index].swizzle = value; break;
    default:
        LOG_TRACE("Viewport[%u].sub[%u] = 0x%x (未使用)", vp_index, sub, value);
        break;
    }

    LOG_TRACE("Viewport[%u].sub[%u] = 0x%x (%.4f)", vp_index, sub, value, fval);
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
        u32 sub = index / MAX_BLEND_TARGETS;
        switch (sub) {
        case 0:  // 独立 blend enable per target
            state_.blend[i].enabled = (value & 1) != 0;
            break;
        case 1: state_.blend[i].color_op  = static_cast<BlendOp>(value); break;
        case 2: state_.blend[i].src_color  = static_cast<BlendFactor>(value); break;
        case 3: state_.blend[i].dst_color  = static_cast<BlendFactor>(value); break;
        case 4: state_.blend[i].alpha_op   = static_cast<BlendOp>(value); break;
        case 5: state_.blend[i].src_alpha   = static_cast<BlendFactor>(value); break;
        case 6: state_.blend[i].dst_alpha   = static_cast<BlendFactor>(value); break;
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

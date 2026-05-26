#pragma once

#include "common/Types.h"
#include "gpu/maxwell/MethodDefs.h"

// ── Maxwell GPU state structures ────────────────────────────
// Mirrors Ryujinx ThreedClassState.cs for Phase 2

// ── Max counts ──────────────────────────────────────────────
constexpr u32 MAX_RENDER_TARGETS    = 8;
constexpr u32 MAX_VIEWPORTS        = 16;
constexpr u32 MAX_VERTEX_ARRAYS    = 16;
constexpr u32 MAX_VERTEX_ATTRIBS   = 32;
constexpr u32 MAX_SHADER_STAGES    = 6;   // VA, VB, TC, TE, GS, FS
constexpr u32 MAX_BLEND_TARGETS    = 8;
constexpr u32 MAX_SCISSORS         = 16;
constexpr u32 MAX_PROGRAMS         = 6;

// ── Color / Depth formats ───────────────────────────────────
enum class RtFormat : u32 {
    RGBA8Unorm      = 0xD5,
    BGRA8Unorm_sRGB = 0xD0,
    RGBA16Float     = 0xCA,
    RGBA32Float     = 0xC0,
    R32Float        = 0xE5,
    R16Float        = 0xF2,
    R8Unorm         = 0xF3,
    RG8Unorm        = 0xEA,
    RG16Float       = 0xDE,
    RG32Float       = 0xCB,
    RGBA16Unorm     = 0xC6,
    Unknown         = 0,
};

enum class DepthFormat : u32 {
    Z32Float       = 0x0A,
    Z16Unorm       = 0x13,
    Z24S8Unorm     = 0x16,
    S8Uint         = 0x17,
    Z32S8X24Float  = 0x19,
    Unknown        = 0,
};

enum class PrimitiveType : u32 {
    Points            = 0,
    Lines             = 1,
    LineLoop          = 2,
    LineStrip         = 3,
    Triangles         = 4,
    TriangleStrip     = 5,
    TriangleFan       = 6,
    Quads             = 7,
    QuadStrip         = 8,
    Polygon           = 9,
    LinesAdjacency    = 10,
    LineStripAdj      = 11,
    TrianglesAdj      = 12,
    TriangleStripAdj  = 13,
    Patches           = 14,
};

enum class CompareOp : u32 {
    Never    = 1,
    Less     = 2,
    Equal    = 3,
    Lequal   = 4,
    Greater  = 5,
    NotEqual = 6,
    Gequal   = 7,
    Always   = 8,
};

enum class FrontFace : u32 {
    CW  = 0x900,
    CCW = 0x901,
};

enum class CullFace : u32 {
    Front        = 0x404,
    Back         = 0x405,
    FrontAndBack = 0x408,
};

enum class PolygonMode : u32 {
    Point = 0x1B00,
    Line  = 0x1B01,
    Fill  = 0x1B02,
};

enum class IndexFormat : u32 {
    Uint8  = 0,
    Uint16 = 1,
    Uint32 = 2,
};

enum class LogicOp : u32 {
    Clear    = 0x1500,
    And      = 0x1501,
    AndReverse = 0x1502,
    Copy     = 0x1503,
    Set      = 0x150F,
};

enum class BlendOp : u32 {
    Add             = 0x8006,
    Subtract        = 0x800A,
    ReverseSubtract = 0x800B,
    Min             = 0x8007,
    Max             = 0x8008,
};

enum class BlendFactor : u32 {
    Zero                = 0,
    One                 = 1,
    SrcColor            = 0x0300,
    OneMinusSrcColor    = 0x0301,
    SrcAlpha            = 0x0302,
    OneMinusSrcAlpha    = 0x0303,
    DstColor            = 0x0306,
    OneMinusDstColor    = 0x0307,
    DstAlpha            = 0x0304,
    OneMinusDstAlpha    = 0x0305,
    SrcAlphaSaturate    = 0x0308,
    ConstantColor       = 0xC001,
    OneMinusConstColor  = 0xC002,
    ConstantAlpha       = 0xC003,
    OneMinusConstAlpha  = 0xC004,
    Src1Color           = 0xC800,
    OneMinusSrc1Color   = 0xC801,
    Src1Alpha           = 0xC802,
    OneMinusSrc1Alpha   = 0xC803,
};

enum class StencilOp : u32 {
    Keep           = 1,
    Zero           = 2,
    Replace        = 3,
    IncrClamp      = 4,
    DecrClamp      = 5,
    Invert         = 6,
    IncrWrap       = 7,
    DecrWrap       = 8,
};

// ── Per-render target state (0x200 array[8]) ────────────────
struct alignas(8) RtState {
    u64      address    = 0;      // IOVA
    u32      width      = 0;      // horizontal
    u32      height     = 0;      // vertical
    RtFormat format     = RtFormat::Unknown;
    u32      tile_mode  = 0;
    u32      array_mode = 0;
    u32      layer_stride = 0;
    u32      base_layer = 0;
};

// ── Viewport transform (0x280 array[16]) ────────────────────
struct ViewportState {
    f32 scale_x    = 0.0f;
    f32 scale_y    = 0.0f;
    f32 scale_z    = 0.0f;
    f32 translate_x = 0.0f;
    f32 translate_y = 0.0f;
    f32 translate_z = 0.0f;
    u32 swizzle    = 0;
};

// ── Scissor state (0x380 array[16]) ─────────────────────────
struct ScissorState {
    bool enabled  = false;
    u32  min_x    = 0;
    u32  max_x    = 0;
    u32  min_y    = 0;
    u32  max_y    = 0;
};

// ── Vertex buffer (0x700 array[16]) ─────────────────────────
struct VertexArrayState {
    u64  address  = 0;   // IOVA
    u32  stride   = 0;
    u32  divisor  = 0;
    bool enabled  = false;
};

// ── Vertex attrib (0x458 array[32]) ─────────────────────────
struct VertexAttribState {
    u32 buffer_index  = 0;  // 绑定的 vertex buffer 索引
    u32 offset        = 0;  // 属性在 buffer 内的偏移 (字节)
    u32 size          = 0;  // 分量数 (1-4: x, xy, xyz, xyzw)
    u32 type          = 0;  // 0=float, 1=sint, 2=uint
    bool is_fixed     = false;
    bool is_bgra      = false;
};

// ── Index buffer state ──────────────────────────────────────
struct IndexBufferState {
    u64         address = 0;
    u64         limit   = 0;     // IOVA 索引缓冲区上限地址
    u32         first   = 0;
    u32         count   = 0;
    IndexFormat format  = IndexFormat::Uint32;
};

// ── Depth/Stencil target ────────────────────────────────────
struct DepthTargetState {
    u64         address     = 0;
    DepthFormat format      = DepthFormat::Unknown;
    u32         width       = 0;
    u32         height      = 0;
    u32         tile_mode   = 0;
    u32         layer_stride = 0;
    bool        enabled     = false;
};

// ── Blend per-target (0x780 array[8]) ───────────────────────
struct BlendState {
    bool      enabled        = false;
    BlendOp   color_op       = BlendOp::Add;
    BlendFactor src_color    = BlendFactor::One;
    BlendFactor dst_color    = BlendFactor::Zero;
    BlendOp   alpha_op       = BlendOp::Add;
    BlendFactor src_alpha    = BlendFactor::One;
    BlendFactor dst_alpha    = BlendFactor::Zero;
    u32       color_mask     = 0xF;  // RGBA write mask
};

// ── Depth/Stencil test ─────────────────────────────────────
struct DepthStencilState {
    bool      depth_enabled  = false;
    bool      depth_write    = true;
    CompareOp depth_func     = CompareOp::Less;
    bool      stencil_enable = false;
    // 前面模板
    CompareOp stencil_front_func = CompareOp::Always;
    u32       stencil_front_ref  = 0;
    u32       stencil_front_mask = 0xFF;
    u32       stencil_front_writemask = 0xFF;
    StencilOp stencil_front_fail = StencilOp::Keep;
    StencilOp stencil_front_zfail = StencilOp::Keep;
    StencilOp stencil_front_zpass = StencilOp::Keep;
    // 背面模板
    bool      stencil_two_side = false;
    CompareOp stencil_back_func  = CompareOp::Always;
    u32       stencil_back_ref   = 0;
    u32       stencil_back_mask  = 0xFF;
    u32       stencil_back_writemask = 0xFF;
    StencilOp stencil_back_fail  = StencilOp::Keep;
    StencilOp stencil_back_zfail = StencilOp::Keep;
    StencilOp stencil_back_zpass = StencilOp::Keep;
    // 深度边界
    bool      depth_bounds_enable = false;
    f32       depth_bounds_near   = 0.0f;
    f32       depth_bounds_far    = 1.0f;
};

// ── Color clear state ──────────────────────────────────────
struct ClearState {
    f32  color[4]   = {0,0,0,0};
    f32  depth      = 1.0f;
    u32  stencil    = 0;
    u32  buffers    = 0;   // ClearBuffers flags
};

// ── Shader state (0x800 array[6]) ──────────────────────────
struct ShaderState {
    u64  offset     = 0;    // Offset into program region
    u32  num_registers = 0;
    bool enabled    = false;
    u32  stage_id   = 0;    // VertexA=0, VertexB=1, TC=2, TE=3, GS=4, FS=5
};

// ── Multisample ─────────────────────────────────────────────
struct MultisampleState {
    bool  enable       = false;
    u32   samples      = 1;
    u32   sample_mask  = 0xFFFFFFFF;
    f32   coverage_to_color = 0;
    bool  alpha_to_coverage = false;
    bool  alpha_to_one     = false;
};

// ── Complete 3D engine state ────────────────────────────────
// This struct holds ALL mutable register state for the 3D engine.
// Mirrors Ryujinx ThreedClassState.
struct alignas(64) GpuState3D {
    // Render targets
    RtState          rt[MAX_RENDER_TARGETS];
    u32              rt_control = 0;

    // Viewports
    ViewportState    viewports[MAX_VIEWPORTS];
    bool             viewport_transform_enable = false;

    // Scissors
    ScissorState     scissors[MAX_SCISSORS];

    // Vertex arrays
    VertexArrayState vertex_arrays[MAX_VERTEX_ARRAYS];

    // Vertex attributes
    VertexAttribState vertex_attribs[MAX_VERTEX_ATTRIBS];

    // Index buffer
    IndexBufferState index_buffer;

    // Depth/Stencil target
    DepthTargetState depth_target;

    // Blend
    BlendState       blend[MAX_BLEND_TARGETS];
    bool             independent_blend = false;
    f32              blend_const[4] = {0,0,0,0};

    // Depth/Stencil test
    DepthStencilState depth_stencil;

    // Clear
    ClearState       clear;

    // Shaders
    ShaderState      shaders[MAX_SHADER_STAGES];

    // Rasterization
    bool             rasterizer_enable      = true;
    PrimitiveType    primitive_type         = PrimitiveType::Triangles;
    PolygonMode      polygon_mode_front     = PolygonMode::Fill;
    PolygonMode      polygon_mode_back      = PolygonMode::Fill;
    CullFace         cull_face              = CullFace::Back;
    FrontFace        front_face             = FrontFace::CCW;
    bool             cull_enable            = false;
    float            line_width             = 1.0f;
    float            point_size             = 1.0f;
    bool             primitive_restart      = false;
    u32              primitive_restart_index = 0;
    bool             depth_clamp            = false;
    bool             rasterizer_discard     = false;
    // 多边形偏移
    bool             polygon_offset_point   = false;
    bool             polygon_offset_line    = false;
    bool             polygon_offset_fill    = false;
    float            polygon_offset_factor  = 0.0f;
    float            polygon_offset_units   = 0.0f;
    float            polygon_offset_clamp   = 0.0f;
    // Alpha test (逐像素裁剪，需色器模拟)
    bool             alpha_test_enable      = false;
    CompareOp        alpha_test_func        = CompareOp::Always;
    float            alpha_test_ref         = 0.0f;
    // 逻辑操作
    bool             logic_op_enable        = false;
    LogicOp          logic_op               = LogicOp::Copy;

    // Multisample
    MultisampleState multisample;

    // Misc
    u32              draw_arrays_first   = 0;
    u32              draw_arrays_count   = 0;
    u32              draw_elements_first  = 0;
    u32              draw_elements_count  = 0;
    u32              vertex_id_base      = 0;
    u32              instance_count      = 1;
    u32              num_instances       = 0;
    bool             prim_restart_draw_arrays = false;

    // Texture pool (needed for TIC/TSC parsing)
    u64              tex_header_pool     = 0;
    u32              tex_header_max_idx  = 0;
    u64              tex_sampler_pool    = 0;
    u32              tex_sampler_max_idx = 0;

    // Program region
    u64              program_region      = 0;
    u64              uniform_buffer_state = 0;
};

static_assert(sizeof(GpuState3D) < 16384, "GpuState3D too large");

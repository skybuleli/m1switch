#pragma once

#include "common/Types.h"

// ── Maxwell engine class IDs ───────────────────────────────
enum class EngineClass : u32 {
    Gpfifo  = 0xB06F,    // Host channel GPFIFO
    _3D     = 0xB197,    // Maxwell 3D
    _2D     = 0x902D,    // Fermi 2D
    Compute = 0xB1C0,    // Maxwell Compute
    Copy    = 0xB0B5,    // Maxwell DMA Copy
};

// ── GPFIFO methods ─────────────────────────────────────────
enum class GpfifoMethod : u32 {
    SemaphoreOffset   = 0x004,
    SemaphorePayload  = 0x006,
    Semaphore         = 0x007,
    MemOpB            = 0x00B,
    SetReference      = 0x014,
    SyncpointPayload  = 0x01C,
    Syncpoint         = 0x01D,
    Yield             = 0x020,
};

// ── 3D engine methods (Maxwell 3D class 0xB197) ──────────────
// Generated from deko3d engine_3d.def
enum class Method3D : u32 {
    NoOperation                    = 0x040,
    WaitForIdle                    = 0x044,
    MmeInstructionRamPointer       = 0x045,
    MmeInstructionRamLoad          = 0x046,
    MmeStartAddressRamPointer      = 0x047,
    MmeStartAddressRamLoad         = 0x048,
    MmeShadowRamControl            = 0x049,
    SetInstrumentationMethodHeader = 0x054,
    SetInstrumentationMethodData   = 0x055,
    LineWidthSeparate              = 0x083,
    SyncptAction                   = 0x0B2,
    SurfaceDecompress              = 0x0B8,
    ZcullUnkFeatureEnable          = 0x0BA,
    SetSpaVersion                  = 0x0C4,
    TessellationMode               = 0x0C8,
    TessellationOuterLevels        = 0x0C9,
    TessellationInnerLevels        = 0x0CD,
    RasterizerEnable               = 0x0DF,
    SetShaderLocalMemory           = 0x1E4,
    SetShaderLocalMemorySize       = 0x1E6,
    ZcullWidth                     = 0x1F0,
    ZcullHeight                    = 0x1F1,
    ZcullDepth                     = 0x1F2,
    ZcullImageSizeAliquots         = 0x1F8,
    ZcullLayerSizeAliquots         = 0x1F9,
    RenderTarget                   = 0x200,  // array[8]
    ViewportTransform              = 0x280,  // array[16]
    Viewport                       = 0x300,  // array[16]
    WindowRectangle                = 0x340,  // array[8]
    SetApiVisibleCallLimit         = 0x359,
    DrawArraysFirst                = 0x35D,
    DrawArraysCount                = 0x35E,
    SetDepthMode                   = 0x35F,
    ClearColor                     = 0x360,
    ClearDepth                     = 0x364,
    ColorReductionEnable           = 0x367,
    ClearStencil                   = 0x368,
    InvalidateShaderCaches         = 0x369,
    SetPolygonModeFront            = 0x36B,
    SetPolygonModeBack             = 0x36C,
    PolygonSmoothEnable            = 0x36D,
    PolygonOffsetPointEnable       = 0x370,
    PolygonOffsetLineEnable        = 0x371,
    PolygonOffsetFillEnable        = 0x372,
    FragmentBarrier                = 0x378,
    PrimitiveRestartWithDrawArrays = 0x37A,
    Scissor                        = 0x380,  // array[16]
    StencilBackFuncRef             = 0x3D5,
    StencilBackMask                = 0x3D6,
    StencilBackFuncMask            = 0x3D7,
    TiledCacheEnable               = 0x3D8,
    InvalidateTextureDataCache     = 0x3DD,
    DiscardRenderTarget            = 0x3DE,
    DepthBoundsNear                = 0x3E7,
    DepthBoundsFar                 = 0x3E8,
    SetMultisampleRasterEnable     = 0x3ED,
    MultisampleRasterSamples       = 0x3EE,
    MultisampleSampleMask          = 0x3EF,
    DepthTargetAddr                = 0x3F8,
    DepthTargetFormat              = 0x3FA,
    DepthTargetTileMode            = 0x3FB,
    DepthTargetLayerStride         = 0x3FC,
    ScreenScissorHorizontal        = 0x3FD,
    ScreenScissorVertical          = 0x3FE,
    ClearBufferFlags               = 0x43E,
    FillRectangleConfig            = 0x44F,
    VertexAttribState              = 0x458,  // array[32]
    MultisampleSampleLocations     = 0x478,
    MultisampleCoverageToColor     = 0x47E,
    RenderTargetControl            = 0x487,
    DepthTargetHorizontal          = 0x48A,
    DepthTargetVertical            = 0x48B,
    DepthTargetArrayMode           = 0x48C,
    InvalidateTextureDataCacheNoWfi = 0x4A2,
    DepthTestEnable                = 0x4B3,
    AlphaToCoverageDither          = 0x4B8,
    IndependentBlendEnable         = 0x4B9,
    DepthWriteEnable               = 0x4BA,
    AlphaTestEnable                = 0x4BB,
    DepthTestFunc                  = 0x4C3,
    AlphaTestRef                   = 0x4C4,
    AlphaTestFunc                  = 0x4C5,
    BlendConstant                  = 0x4C7,
    ColorBlendEnable               = 0x4D8,  // array[8]
    StencilEnable                  = 0x4E0,
    StencilFrontOpFail             = 0x4E1,
    StencilFrontOpZFail            = 0x4E2,
    StencilFrontOpZPass            = 0x4E3,
    StencilFrontFunc               = 0x4E4,
    StencilFrontFuncRef            = 0x4E5,
    StencilFrontFuncMask           = 0x4E6,
    StencilFrontMask               = 0x4E7,
    SetWindowOrigin                = 0x4EB,
    LineWidthSmooth                = 0x4EC,
    LineWidthAliased               = 0x4ED,
    ClipDistanceEnable             = 0x544,
    PointSpriteSize                = 0x546,
    PointSpriteEnable              = 0x548,
    ResetCounter                   = 0x54C,
    MultisampleEnable              = 0x54D,
    DepthTargetEnable              = 0x54E,
    MultisampleControl             = 0x54F,
    SetTexSamplerPool              = 0x557,
    SetTexSamplerPoolMaximumIndex  = 0x559,
    PolygonOffsetFactor            = 0x55B,
    SetTexHeaderPool               = 0x55D,
    SetTexHeaderPoolMaximumIndex   = 0x55F,
    StencilTwoSideEnable           = 0x565,
    StencilBackOpFail              = 0x566,
    StencilBackOpZFail             = 0x567,
    StencilBackOpZPass             = 0x568,
    StencilBackFunc                = 0x569,
    PolygonOffsetUnits             = 0x56F,
    SetRenderLayer                 = 0x573,
    MultisampleMode                = 0x574,
    SetProgramRegion               = 0x582,
    VertexBeginGl                  = 0x586,
    PrimitiveRestartEnable         = 0x591,
    PrimitiveRestartIndex          = 0x592,
    VertexIdConfig                 = 0x593,
    ProvokingVertexLast            = 0x5A1,
    IndexArrayStartIova            = 0x5F2,
    IndexArrayLimitIova            = 0x5F4,
    IndexArrayFormat               = 0x5F6,
    DrawElementsFirst              = 0x5F7,
    DrawElementsCount              = 0x5F8,
    PolygonOffsetClamp             = 0x61F,
    IsVertexArrayPerInstance       = 0x620,  // array[16]
    VertexProgramPointSize         = 0x644,
    CullFaceEnable                 = 0x646,
    SetFrontFace                   = 0x647,
    SetCullFace                    = 0x648,
    ViewportTransformEnable        = 0x64B,
    ViewVolumeClipControl          = 0x64F,
    DepthBoundsEnable              = 0x66F,
    ColorLogicOpEnable             = 0x671,
    ColorLogicOpType               = 0x672,
    ClearBuffers                   = 0x674,
    ColorWriteMask                 = 0x680,  // array[8]
    PipeNop                        = 0x68B,
    SetReportSemaphoreOffset       = 0x6C0,
    SetReportSemaphorePayload      = 0x6C2,
    SetReportSemaphore             = 0x6C3,
    VertexArray                    = 0x700,  // array[16]
    IndependentBlend               = 0x780,  // array[8]
    VertexArrayLimit               = 0x7C0,  // array[16]
    SetProgram                     = 0x800,  // array[6]
    FirmwareCall                   = 0x8C0,  // array[32]
    ConstbufSelectorSize           = 0x8E0,
    ConstbufSelectorAddr           = 0x8E1,
    LoadConstbufOffset             = 0x8E3,
    LoadConstbufData               = 0x8E4,  // array[16]
    Bind                           = 0x900,  // array[5]
    SetBindlessTexture             = 0x982,
    MmeFirmwareArgs                = 0xD00,  // array[8]
    MmeScratch                     = 0xD08,  // array[16]
    MmeProgramOffsets              = 0xD20,
};

// ── 3D method array sizes ───────────────────────────────────
constexpr u32 METHOD_ARRAY_LENS[] = {
    0x200, 8,     // RenderTarget[8]
    0x280, 16,    // ViewportTransform[16]
    0x300, 16,    // Viewport[16]
    0x340, 8,     // WindowRectangle[8]
    0x380, 16,    // Scissor[16]
    0x458, 32,    // VertexAttribState[32]
    0x4D8, 8,     // ColorBlendEnable[8]
    0x620, 16,    // IsVertexArrayPerInstance[16]
    0x680, 8,     // ColorWriteMask[8]
    0x700, 16,    // VertexArray[16]
    0x780, 8,     // IndependentBlend[8]
    0x7C0, 16,    // VertexArrayLimit[16]
    0x800, 6,     // SetProgram[6] (VS-A, VS-B, TC, TE, GS, FS)
    0x8C0, 32,    // FirmwareCall[32]
    0x8E4, 16,    // LoadConstbufData[16]
    0x900, 5,     // Bind[5]
    0xD00, 8,     // MmeFirmwareArgs[8]
    0xD08, 16,    // MmeScratch[16]
    0,0,          // terminator
};

// ── 2D engine methods ──────────────────────────────────────
enum class Method2D : u32 {
    SetDstFormat                = 0x080,
    SetDstMemoryLayout          = 0x081,
    SetDstBlockSize             = 0x082,
    SetDstDepth                 = 0x083,
    SetDstLayer                 = 0x084,
    SetDstPitch                 = 0x085,
    SetDstWidth                 = 0x086,
    SetDstHeight                = 0x087,
    SetDstOffset                = 0x088,
    SetSrcFormat                = 0x08C,
    SetSrcMemoryLayout          = 0x08D,
    SetSrcBlockSize             = 0x08E,
    SetSrcDepth                 = 0x08F,
    SetSrcPitch                 = 0x091,
    SetSrcWidth                 = 0x092,
    SetSrcHeight                = 0x093,
    SetSrcOffset                = 0x094,
    SetClipEnable               = 0x0A4,
    SetBeta4                    = 0x0AA,
    SetOperation                = 0x0AB,
    SetCompressionEnable        = 0x0B5,
    SetPixelsFromMemoryDstX0    = 0x22C,
    SetPixelsFromMemoryDstY0    = 0x22D,
    PixelsFromMemorySrcY0Int    = 0x237,  // triggers operation
};

// ── DMA/Copy methods ───────────────────────────────────────
enum class MethodDma : u32 {
    LaunchDma    = 0x0C0,
    OffsetIn     = 0x100,
    OffsetOut    = 0x102,
    PitchIn      = 0x104,
    PitchOut     = 0x105,
    LineLengthIn = 0x106,
    LineCount    = 0x107,
    SetDstBlockSize = 0x1C3,
    SetDstWidth     = 0x1C4,
    SetSrcBlockSize = 0x1CA,
};

// ── Compute methods ────────────────────────────────────────
enum class MethodCompute : u32 {
    WaitForIdle                       = 0x044,
    SetShaderSharedMemoryWindow       = 0x085,
    InvalidateShaderCaches            = 0x087,
    SendPcasA                         = 0x0AD,
    SendPcasB                         = 0x0AE,
    SetShaderLocalMemory              = 0x1E4,
    SetTexSamplerPool                 = 0x557,
    SetTexHeaderPool                  = 0x55D,
    SetProgramRegion                  = 0x582,
    SetReportSemaphoreOffset          = 0x6C0,
    SetBindlessTexture                = 0x982,
};

// ── Helper: get method name for logging ─────────────────────
const char* Method3DName(u32 method);

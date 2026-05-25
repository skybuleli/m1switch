#include "gpu/maxwell/MethodDefs.h"

// ── Method name lookup ─────────────────────────────────────
#define METHOD_CASE(m) case u32(Method3D::m): return #m

const char* Method3DName(u32 method) {
    switch (method) {
    METHOD_CASE(NoOperation);
    METHOD_CASE(WaitForIdle);
    METHOD_CASE(MmeInstructionRamPointer);
    METHOD_CASE(MmeInstructionRamLoad);
    METHOD_CASE(MmeStartAddressRamPointer);
    METHOD_CASE(MmeStartAddressRamLoad);
    METHOD_CASE(MmeShadowRamControl);
    METHOD_CASE(LineWidthSeparate);
    METHOD_CASE(SyncptAction);
    METHOD_CASE(SurfaceDecompress);
    METHOD_CASE(SetSpaVersion);
    METHOD_CASE(TessellationMode);
    METHOD_CASE(RasterizerEnable);
    METHOD_CASE(SetShaderLocalMemory);
    METHOD_CASE(SetShaderLocalMemorySize);
    METHOD_CASE(RenderTarget);
    METHOD_CASE(ViewportTransform);
    METHOD_CASE(Viewport);
    METHOD_CASE(WindowRectangle);
    METHOD_CASE(DrawArraysFirst);
    METHOD_CASE(DrawArraysCount);
    METHOD_CASE(SetDepthMode);
    METHOD_CASE(ClearColor);
    METHOD_CASE(ClearDepth);
    METHOD_CASE(ClearStencil);
    METHOD_CASE(InvalidateShaderCaches);
    METHOD_CASE(SetPolygonModeFront);
    METHOD_CASE(SetPolygonModeBack);
    METHOD_CASE(PolygonOffsetPointEnable);
    METHOD_CASE(PolygonOffsetLineEnable);
    METHOD_CASE(PolygonOffsetFillEnable);
    METHOD_CASE(FragmentBarrier);
    METHOD_CASE(Scissor);
    METHOD_CASE(StencilBackFuncRef);
    METHOD_CASE(StencilBackMask);
    METHOD_CASE(TiledCacheEnable);
    METHOD_CASE(InvalidateTextureDataCache);
    METHOD_CASE(InvalidateTextureDataCacheNoWfi);
    METHOD_CASE(DepthBoundsNear);
    METHOD_CASE(DepthBoundsFar);
    METHOD_CASE(SetMultisampleRasterEnable);
    METHOD_CASE(MultisampleRasterSamples);
    METHOD_CASE(DepthTargetAddr);
    METHOD_CASE(DepthTargetFormat);
    METHOD_CASE(DepthTargetTileMode);
    METHOD_CASE(DepthTargetLayerStride);
    METHOD_CASE(ScreenScissorHorizontal);
    METHOD_CASE(ScreenScissorVertical);
    METHOD_CASE(DepthTestEnable);
    METHOD_CASE(DepthWriteEnable);
    METHOD_CASE(AlphaTestEnable);
    METHOD_CASE(DepthTestFunc);
    METHOD_CASE(AlphaTestRef);
    METHOD_CASE(AlphaTestFunc);
    METHOD_CASE(BlendConstant);
    METHOD_CASE(ColorBlendEnable);
    METHOD_CASE(StencilEnable);
    METHOD_CASE(StencilFrontFunc);
    METHOD_CASE(StencilFrontFuncRef);
    METHOD_CASE(StencilFrontMask);
    METHOD_CASE(SetWindowOrigin);
    METHOD_CASE(ClipDistanceEnable);
    METHOD_CASE(PointSpriteEnable);
    METHOD_CASE(ResetCounter);
    METHOD_CASE(MultisampleEnable);
    METHOD_CASE(DepthTargetEnable);
    METHOD_CASE(SetTexSamplerPool);
    METHOD_CASE(SetTexHeaderPool);
    METHOD_CASE(SetProgramRegion);
    METHOD_CASE(VertexAttribState);
    METHOD_CASE(VertexArray);
    METHOD_CASE(IndependentBlend);
    METHOD_CASE(SetProgram);
    METHOD_CASE(Bind);
    METHOD_CASE(SetBindlessTexture);
    METHOD_CASE(ClearBuffers);
    METHOD_CASE(ColorWriteMask);
    METHOD_CASE(VertexBeginGl);
    METHOD_CASE(PrimitiveRestartEnable);
    METHOD_CASE(PrimitiveRestartIndex);
    METHOD_CASE(IndexArrayStartIova);
    METHOD_CASE(IndexArrayFormat);
    METHOD_CASE(DrawElementsFirst);
    METHOD_CASE(DrawElementsCount);
    METHOD_CASE(CullFaceEnable);
    METHOD_CASE(SetFrontFace);
    METHOD_CASE(SetCullFace);
    METHOD_CASE(ViewportTransformEnable);
    METHOD_CASE(ViewVolumeClipControl);
    METHOD_CASE(DepthBoundsEnable);
    METHOD_CASE(ColorLogicOpEnable);
    METHOD_CASE(SetReportSemaphoreOffset);
    METHOD_CASE(SetReportSemaphorePayload);
    METHOD_CASE(SetReportSemaphore);
    METHOD_CASE(PipeNop);
    METHOD_CASE(ConstbufSelectorSize);
    METHOD_CASE(ConstbufSelectorAddr);
    METHOD_CASE(LoadConstbufData);
    // METHOD_CASE(DrawTextureSrcY);  // aliased, added if needed
    METHOD_CASE(StencilTwoSideEnable);
    METHOD_CASE(FillRectangleConfig);
    METHOD_CASE(IndependentBlendEnable);
    METHOD_CASE(ColorReductionEnable);
    default: return "Unknown3D";
    }
}

#undef METHOD_CASE

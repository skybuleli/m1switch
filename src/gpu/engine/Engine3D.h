#pragma once

#include "common/Types.h"
#include "gpu/maxwell/GpuState.h"

// ── 3D Engine method handler ───────────────────────────────
// Processes 3D engine method writes by updating GpuState3D.
// Each method ID maps to a specific state field.

class Engine3D {
public:
    Engine3D();

    // Process a method write to the 3D engine
    // @param method  Method ID (from Method3D enum)
    // @param value   32-bit value written
    // @param is_draw If true, this method triggers a draw
    void HandleMethod(u32 method, u32 value, bool& is_draw);

    // Get current state (mutable for reads)
    GpuState3D& State() { return state_; }
    const GpuState3D& State() const { return state_; }

    // Get a register value (for read/modify/write methods)
    u32 GetRegister(u32 method) const;

    // Reset state to defaults
    void Reset();

private:
    GpuState3D state_;

    // Handle specific method groups
    void HandleRenderTarget(u32 index, u32 value);
    void HandleViewport(u32 index, u32 value);
    void HandleScissor(u32 index, u32 value);
    void HandleVertexArray(u32 index, u32 value);
    void HandleVertexAttrib(u32 index, u32 value);
    void HandleBlend(u32 index, u32 value);
    void HandleShader(u32 index, u32 value);
};

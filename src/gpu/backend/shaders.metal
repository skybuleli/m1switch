// Phase 5 fixed shaders — no dependency on Maxwell shader recompiler.
// These will be replaced by Phase 4's Maxwell→MSL compiler.

#include <metal_stdlib>
using namespace metal;

// ── Vertex input structure ──────────────────────────────────
struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
};

// ── Vertex output / Fragment input ─────────────────────────
struct VertexOut {
    float4 position [[position]];
    float4 color;
};

// ── Vertex shader: pass-through with position and color ─────
vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant float2& offset [[buffer(1)]]) {
    VertexOut out;
    out.position = float4(in.position + offset, 0.0, 1.0);
    out.color = in.color;
    return out;
}

// ── Fragment shader: pass-through color ─────────────────────
fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    return in.color;
}

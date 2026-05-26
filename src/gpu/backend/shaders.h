// Embedded MSL shader source (compiled at runtime by Metal)
// Phase 5: fixed shaders — vertex + fragment passthrough

#pragma once

constexpr const char* kMetalShaders = R"(
#include <metal_stdlib>
using namespace metal;

// ── Vertex shader (colored triangle / general) ───────────
struct VertexIn {
    float2 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant float2& offset [[buffer(1)]]) {
    VertexOut out;
    out.position = float4(in.position + offset, 0.0, 1.0);
    out.color = in.color;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]]) {
    return in.color;
}

// ── Full-screen quad shaders (VI framebuffer blit) ──────
struct QuadVertexOut {
    float4 position [[position]];
    float2 texcoord;
};

vertex QuadVertexOut fb_vertex_main(uint vid [[vertex_id]]) {
    // Full-screen triangle strip quad (4 vertices)
    float2 pos[] = {
        float2(-1, -1),  // bottom-left
        float2( 1, -1),  // bottom-right
        float2(-1,  1),  // top-left
        float2( 1,  1),  // top-right
    };
    float2 tc[] = {
        float2(0, 1),   // bottom-left
        float2(1, 1),   // bottom-right
        float2(0, 0),   // top-left
        float2(1, 0),   // top-right
    };

    QuadVertexOut out;
    out.position = float4(pos[vid], 0.0, 1.0);
    out.texcoord = tc[vid];
    return out;
}

fragment float4 fb_fragment_main(QuadVertexOut in [[stage_in]],
                                  texture2d<float> fb_tex [[texture(0)]],
                                  sampler fb_sampler [[sampler(0)]]) {
    return fb_tex.sample(fb_sampler, in.texcoord);
}
)";

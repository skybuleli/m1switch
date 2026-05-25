// Embedded MSL shader source (compiled at runtime by Metal)
// Phase 5: fixed shaders — vertex + fragment passthrough

#pragma once

constexpr const char* kMetalShaders = R"(
#include <metal_stdlib>
using namespace metal;

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
)";

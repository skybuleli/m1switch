// Phase 2: GPU Parser Integration Test
// Tests GPFifo parsing, 3D engine method dispatch, state tracking.

#include "common/Log.h"
#include "gpu/StateTracker.h"
#include "gpu/engine/GPFifo.h"
#include "gpu/engine/Engine3D.h"
#include "gpu/maxwell/GpuState.h"

#include <cstdio>
#include <cstring>

// ── Test 1: GPFifo inline command ───────────────────────────
bool Test_GPFifoInline() {
    // Inline mode: header with mode=4, method=0x100, arg=0x42, subch=1
    u32 header = GPFifo::MakeHeader(GPFifo::Inline, 0x100, 0x42, 1);

    u32 words[] = {header};
    GPFifo fifo;

    u32 last_method = 0;
    u32 last_value = 0;
    u32 last_subch = 0;
    fifo.SetCallback([&](u32 subch, u32 m, u32 v) {
        last_subch = subch; last_method = m; last_value = v;
    });

    fifo.Process(words);
    if (last_method != 0x100 || last_value != 0x42 || last_subch != 1) {
        printf("  FAIL: got subch=%u method=0x%x val=0x%x\n",
               last_subch, last_method, last_value);
        return false;
    }
    printf("  PASS: Inline command decoded correctly\n");
    return true;
}

// ── Test 2: GPFifo increasing mode ──────────────────────────
bool Test_GPFifoIncreasing() {
    // Increasing mode: method=0x200, arg=4 (4 values), subch=0
    u32 header = GPFifo::MakeHeader(GPFifo::Increasing, 0x200, 4, 0);
    u32 words[] = {header, 0x10, 0x20, 0x30, 0x40};

    GPFifo fifo;
    int count = 0;
    u32 expected[] = {0x10, 0x20, 0x30, 0x40};
    fifo.SetCallback([&](u32 subch, u32 m, u32 v) {
        if (m != 0x200 + count || v != expected[count]) {
            printf("  FAIL at %d: method=0x%x val=0x%x\n", count, m, v);
        }
        count++;
    });

    fifo.Process(words);
    if (count != 4) { printf("  FAIL: expected 4 callbacks, got %d\n", count); return false; }
    printf("  PASS: Increasing mode\n");
    return true;
}

// ── Test 3: 3D Engine method dispatch ──────────────────────
bool Test_Engine3D() {
    Engine3D engine;
    bool is_draw = false;

    // Set rasterizer
    engine.HandleMethod(u32(Method3D::RasterizerEnable), 1, is_draw);
    if (!engine.State().rasterizer_enable) {
        printf("  FAIL: rasterizer not enabled\n");
        return false;
    }

    // Set front face
    engine.HandleMethod(u32(Method3D::SetFrontFace), u32(FrontFace::CW), is_draw);
    if (engine.State().front_face != FrontFace::CW) {
        printf("  FAIL: front face not set\n");
        return false;
    }

    // Set blend enable
    engine.HandleMethod(u32(Method3D::ColorBlendEnable), 1, is_draw);
    if (!engine.State().blend[0].enabled) {
        printf("  FAIL: blend not enabled\n");
        return false;
    }

    // Set depth func
    engine.HandleMethod(u32(Method3D::DepthTestFunc), u32(CompareOp::Lequal), is_draw);
    if (engine.State().depth_stencil.depth_func != CompareOp::Lequal) {
        printf("  FAIL: depth func\n");
        return false;
    }

    // Draw trigger
    engine.HandleMethod(u32(Method3D::DrawArraysCount), 42, is_draw);
    if (!is_draw || engine.State().draw_arrays_count != 42) {
        printf("  FAIL: draw not triggered\n");
        return false;
    }

    printf("  PASS: 3D engine state updated correctly\n");
    return true;
}

// ── Test 4: Full pipeline ──────────────────────────────────
bool Test_FullPipeline() {
    // Simulate a minimal pushbuffer sequence that sets up
    // state and triggers a draw
    u32 pushbuffer[] = {
        // Enable rasterizer (Inline, method=0x0DF, val=1, subch=0 = Engine3D)
        GPFifo::MakeHeader(GPFifo::Inline, u32(Method3D::RasterizerEnable), 1, 0),
        // Set depth test func to Less (Inline, method=0x4C3, val=2, subch=0)
        GPFifo::MakeHeader(GPFifo::Inline, u32(Method3D::DepthTestFunc), 2, 0),
        // Set blend enable (Inline, method=0x4D8, val=1, subch=0)
        GPFifo::MakeHeader(GPFifo::Inline, u32(Method3D::ColorBlendEnable), 1, 0),
        // Draw (Inline, method=0x35E, val=123, subch=0)
        GPFifo::MakeHeader(GPFifo::Inline, u32(Method3D::DrawArraysCount), 123, 0),
    };

    StateTracker tracker;
    size_t consumed = tracker.PushBuffer(pushbuffer);

    if (consumed != 4) {
        printf("  FAIL: consumed %zu words (expected 4)\n", consumed);
        return false;
    }

    const auto& state = tracker.GetState3D();
    if (!state.rasterizer_enable) {
        printf("  FAIL: rasterizer not enabled\n");
        return false;
    }
    auto draws = tracker.ConsumeDraws();
    if (draws.size() != 1) {
        printf("  FAIL: draw queue size = %zu (expected 1)\n", draws.size());
        return false;
    }
    if (draws[0].arrays_count != 123) {
        printf("  FAIL: draw count = %u (expected 123)\n", draws[0].arrays_count);
        return false;
    }
    if (!tracker.IsDirty()) {
        printf("  FAIL: dirty flag not set\n");
        return false;
    }

    printf("  PASS: Full pipeline (%zu words)\n", consumed);
    return true;
}

int main() {
    Log::Init();
    printf("=== Phase 2 GPU Parser Tests ===\n\n");

    bool pass = true;
    pass &= Test_GPFifoInline();
    pass &= Test_GPFifoIncreasing();
    pass &= Test_Engine3D();
    pass &= Test_FullPipeline();

    printf("\n%s\n", pass ? "=== ALL PASSED ===" : "=== SOME FAILED ===");
    return pass ? 0 : 1;
}

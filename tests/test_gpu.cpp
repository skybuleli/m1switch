// ── GPU parser tests ───────────────────────────────────────

#include "gpu/StateTracker.h"
#include "gpu/engine/GPFifo.h"
#include "gpu/engine/Engine3D.h"
#include "gpu/maxwell/GpuState.h"

TEST(GPFifo_Inline) {
    GPFifo fifo;
    u32 header = GPFifo::MakeHeader(GPFifo::Inline, 0x100, 0x42, 1);

    u32 last_m = 0, last_v = 0, last_s = 0;
    fifo.SetCallback([&](u32 s, u32 m, u32 v) {
        last_s = s; last_m = m; last_v = v;
    });

    fifo.Process({&header, 1});
    CHECK_EQ(1, last_s);
    CHECK_EQ(0x100, last_m);
    CHECK_EQ(0x42, last_v);

    return true;
}

TEST(GPFifo_Increasing) {
    GPFifo fifo;
    u32 words[] = {
        GPFifo::MakeHeader(GPFifo::Increasing, 0x200, 3, 0),
        0x10, 0x20, 0x30,
    };

    int count = 0;
    u32 expected[] = {0x10, 0x20, 0x30};
    fifo.SetCallback([&](u32 s, u32 m, u32 v) {
        CHECK_EQ(0x200 + count, m);
        CHECK_EQ(expected[count], v);
        count++;
    });

    fifo.Process(words);
    CHECK_EQ(3, count);

    return true;
}

TEST(Engine3D_State) {
    Engine3D engine;
    bool is_draw = false;

    engine.HandleMethod(u32(Method3D::RasterizerEnable), 1, is_draw);
    CHECK(engine.State().rasterizer_enable);

    engine.HandleMethod(u32(Method3D::SetFrontFace), u32(FrontFace::CW), is_draw);
    CHECK_EQ(FrontFace::CW, engine.State().front_face);

    engine.HandleMethod(u32(Method3D::DrawArraysCount), 42, is_draw);
    CHECK(is_draw);
    CHECK_EQ(42, engine.State().draw_arrays_count);

    return true;
}

TEST(StateTracker_FullPipeline) {
    u32 pushbuffer[] = {
        GPFifo::MakeHeader(GPFifo::Inline,
            u32(Method3D::RasterizerEnable), 1, 0),
        GPFifo::MakeHeader(GPFifo::Inline,
            u32(Method3D::DepthTestFunc), u32(CompareOp::Lequal), 0),
        GPFifo::MakeHeader(GPFifo::Inline,
            u32(Method3D::DrawArraysCount), 123, 0),
    };

    StateTracker tracker;
    size_t consumed = tracker.PushBuffer(pushbuffer);
    CHECK_EQ(3, consumed);

    // State was applied
    const auto& state = tracker.GetState3D();
    CHECK(state.rasterizer_enable);
    CHECK(tracker.IsDirty());

    // Draw was enqueued (counts reset at enqueue time for batching)
    CHECK(tracker.HasPendingDraws());
    auto draws = tracker.ConsumeDraws();
    CHECK_EQ(1, draws.size());
    CHECK_EQ(123, draws[0].arrays_count);
    CHECK_EQ(0, state.draw_arrays_count);  // reset after enqueue

    return true;
}

#include "gpu/StateTracker.h"
#include "common/Log.h"

StateTracker::StateTracker() {
    // Set up GPFifo dispatch callback
    gpfifo_.SetCallback([this](u32 subch, u32 method, u32 value) {
        this->OnMethod(subch, method, value);
    });
}

// ── Push buffer ─────────────────────────────────────────────
size_t StateTracker::PushBuffer(std::span<const u32> words) {
    LOG_TRACE("PushBuffer: %zu words", words.size());
    return gpfifo_.Process(words);
}

// ── Method dispatch ─────────────────────────────────────────
void StateTracker::OnMethod(u32 subch, u32 method, u32 value) {
    // Route to the appropriate engine based on subchannel
    GPFifo::EngineType engine = gpfifo_.GetEngine(subch);

    switch (engine) {
    case GPFifo::Engine3D: {
        bool is_draw = false;
        engine_3d_.HandleMethod(method, value, is_draw);

        // Mark dirty (Phase 5: map method → specific dirty bit)
        dirty_flags_ = 1;

        if (is_draw) {
            LOG_INFO("Draw triggered: method=0x%x value=%u", method, value);
            // Phase 5: submit draw to Metal backend
        }
        break;
    }

    case GPFifo::Engine2D:
        LOG_TRACE("2D engine method 0x%x=0x%x", method, value);
        break;

    case GPFifo::EngineDma:
        LOG_TRACE("DMA method 0x%x=0x%x", method, value);
        break;

    case GPFifo::EngineInline:
        LOG_TRACE("Inline method 0x%x=0x%x", method, value);
        break;

    case GPFifo::EngineCompute:
        LOG_TRACE("Compute method 0x%x=0x%x", method, value);
        break;

    default:
        LOG_WARN("Unknown engine %u for method 0x%x", (u32)engine, method);
        break;
    }
}

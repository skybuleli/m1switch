#include "gpu/StateTracker.h"
#include "common/Log.h"
#include "debug/TraceEngine.h"

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

// ── Pipeline key ────────────────────────────────────────────
// Computes a hash of state that determines the Metal pipeline.
// Draws sharing the same key can be batched in one encoder.
u64 StateTracker::ComputePipelineKey(const GpuState3D& state) {
    u64 key = state.program_region;
    key ^= state.shaders[0].offset << 12;
    key ^= state.shaders[5].offset << 24;
    key ^= (u64)state.primitive_type << 8;
    // Include RT configuration (format of active RTs)
    for (u32 i = 0; i < MAX_RENDER_TARGETS; i++) {
        if (state.rt[i].address != 0 && state.rt[i].width > 0) {
            key ^= (u64)state.rt[i].format << (i * 4 + 40);
        }
    }
    // Include depth format if enabled
    if (state.depth_target.enabled) {
        key ^= (u64)state.depth_target.format << 56;
    }
    // Include blend state for RT[0] (most common)
    if (state.blend[0].enabled) {
        key ^= ((u64)state.blend[0].color_op << 16) ^
               ((u64)state.blend[0].src_color << 20) ^
               ((u64)state.blend[0].dst_color << 24) ^
               ((u64)state.blend[0].alpha_op << 28) ^
               ((u64)state.blend[0].src_alpha << 32) ^
               ((u64)state.blend[0].dst_alpha << 36);
    }
    // Include color write mask
    key ^= (u64)state.blend[0].color_mask << 48;
    // Include depth/stencil state
    if (state.depth_stencil.depth_enabled) {
        key ^= (u64)state.depth_stencil.depth_func << 38;
        key ^= (u64)state.depth_stencil.depth_write << 42;
    }
    return key;
}

// ── Method dispatch ─────────────────────────────────────────
void StateTracker::OnMethod(u32 subch, u32 method, u32 value) {
    // Route to the appropriate engine based on subchannel
    GPFifo::EngineType engine = gpfifo_.GetEngine(subch);

    switch (engine) {
    case GPFifo::Engine3D: {
        bool is_draw = false;
        engine_3d_.HandleMethod(method, value, is_draw);

        // Mark dirty
        dirty_flags_ = 1;

        if (is_draw) {
            // Snapshot draw parameters and push to the queue
            auto& st = engine_3d_.State();
            PendingDraw draw;
            draw.primitive_type  = st.primitive_type;
            draw.arrays_first    = st.draw_arrays_first;
            draw.arrays_count    = st.draw_arrays_count;
            draw.elements_first  = st.draw_elements_first;
            draw.elements_count  = st.draw_elements_count;
            draw.index_addr      = st.index_buffer.address;
            draw.index_format    = st.index_buffer.format;
            draw.index_count     = st.draw_elements_count;
            draw.pipeline_key    = ComputePipelineKey(st);

            draw_queue_.push_back(draw);

            // Reset live draw counts so they don't repeat
            st.draw_arrays_count = 0;
            st.draw_elements_count = 0;
            st.index_buffer.count = 0;

            // ── GPU 追踪 ────────────────────────────────────
            TRACE_GPU(method, (u64)draw.arrays_count,
                      draw.pipeline_key,
                      (u64)draw.elements_count);

            LOG_INFO("Draw enqueued: method=0x%x value=%u (key=0x%llx, queue=%zu)",
                     method, value, draw.pipeline_key, draw_queue_.size());
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

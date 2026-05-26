#pragma once

#include "common/Types.h"
#include "gpu/maxwell/GpuState.h"
#include "gpu/engine/Engine3D.h"
#include "gpu/engine/GPFifo.h"
#include "memory/Memory.h"
#include <vector>

// ── GPU State Tracker ──────────────────────────────────────
// Orchestrates the full pipeline:
//   GPFifo parser → Engine dispatch → State updates → Dirty tracking
//
// Phase 2: tracks state changes. Phase 5: drives Metal backend.

class StateTracker {
public:
    StateTracker();

    // Push raw pushbuffer data for processing
    size_t PushBuffer(std::span<const u32> words);

    // ── State access ─────────────────────────────────
    const GpuState3D& GetState3D() const { return engine_3d_.State(); }
    GpuState3D& GetState3D() { return engine_3d_.State(); }
    Memory* GetMemory() const { return memory_; }
    void SetMemory(Memory* mem) { memory_ = mem; }
    GPFifo& GetGPFifo() { return gpfifo_; }

    // ── PendingDraw snapshot ─────────────────────────
    // Captured at the moment a draw command is triggered.
    // MetalRenderer reads the queue and batches consecutive draws
    // with the same pipeline state into a single encoder.
    struct PendingDraw {
        PrimitiveType primitive_type = PrimitiveType::Triangles;
        u32 arrays_first   = 0;
        u32 arrays_count   = 0;
        u32 elements_first = 0;
        u32 elements_count = 0;
        u64 index_addr     = 0;
        IndexFormat index_format = IndexFormat::Uint32;
        u32 index_count    = 0;
        // Pipeline key for batching: draws with the same key
        // can share a render pass encoder.
        u64 pipeline_key   = 0;
    };

    // ── Dirty tracking ───────────────────────────────
    // is_dirty: any 3D state has changed since last frame
    // draw_queue_: pending draw commands not yet rendered
    u64 GetDirtyFlags() const { return dirty_flags_; }

    // Returns true if there are pending draws
    bool HasPendingDraws() const { return !draw_queue_.empty(); }
    // Single-draw compatibility (Phase 5 legacy)
    bool HasPendingDraw() const { return HasPendingDraws(); }
    // Returns true if any state has changed since last ClearDirty()
    bool IsDirty() const { return dirty_flags_ != 0; }

    // Consume the entire draw queue for batch rendering
    // The caller takes ownership of the snapshot — draw state is reset.
    std::vector<PendingDraw> ConsumeDraws() { return std::move(draw_queue_); }

    // Assumes single-threaded: called from render loop, not concurrent with PushBuffer.
    void ClearDirty() {
        dirty_flags_ = 0;
        draw_queue_.clear();
    }

private:
    // Method dispatch callback for GPFifo
    void OnMethod(u32 subch, u32 method, u32 value);

    GPFifo gpfifo_;
    Engine3D engine_3d_;

    // Subchannel to engine mapping
    u32 subch_engine_map_[8] = {};
    Memory* memory_ = nullptr;

    // Dirty flags (one bit per state group)
    u64 dirty_flags_ = 0;
    // Draw queue for batching
    std::vector<PendingDraw> draw_queue_;

    // Compute a pipeline key from current GPU state
    // Draws with the same key can share a Metal render pass encoder.
    static u64 ComputePipelineKey(const GpuState3D& state);
};

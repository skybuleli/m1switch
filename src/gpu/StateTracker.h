#pragma once

#include "common/Types.h"
#include "gpu/maxwell/GpuState.h"
#include "gpu/engine/Engine3D.h"
#include "gpu/engine/GPFifo.h"
#include "memory/Memory.h"

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

    // ── Dirty tracking ───────────────────────────────
    // Bitmask of dirty register groups (Phase 5+)
    u64 GetDirtyFlags() const { return dirty_flags_; }
    void ClearDirty() { dirty_flags_ = 0; }
    bool IsDirty() const { return dirty_flags_ != 0; }

private:
    // Method dispatch callback for GPFifo
    void OnMethod(u32 subch, u32 method, u32 value);

    GPFifo gpfifo_;
    Engine3D engine_3d_;

    // Subchannel to engine mapping
    u32 subch_engine_map_[8] = {};
    Memory* memory_ = nullptr;

    // Dirty flags (one bit per state group, Phase 5)
    u64 dirty_flags_ = 0;
};

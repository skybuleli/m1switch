#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "gpu/maxwell/MethodDefs.h"
#include "gpu/maxwell/GpuState.h"

#include <span>
#include <functional>

// ── GPFifo pushbuffer parser ───────────────────────────────
// Decodes the command stream and dispatches to engine handlers.
//
// Pushbuffer word format:
//   [31:29] Mode (INC=1, NON_INC=3, INLINE=4, ONCE=5)
//   [28:16] Arg count / inline data
//   [15:13] Subchannel ID
//   [12:0]  Method ID

class GPFifo {
public:
    // Submission modes
    enum Mode : u32 {
        Increasing      = 1,
        NonIncreasing   = 3,
        Inline          = 4,
        IncreaseOnce    = 5,
    };

    // Engine types (subchannel mapping)
    enum EngineType : u32 {
        Engine3D     = 0,
        Engine2D     = 1,
        EngineDma    = 2,
        EngineInline = 3,
        EngineCompute= 4,
    };

    // Callback when a method+value pair is decoded
    // args: subchannel, method_id, value
    using MethodCallback = std::function<void(u32 subch, u32 method, u32 value)>;

    GPFifo();

    // Set the callback for method writes
    void SetCallback(MethodCallback cb) { callback_ = std::move(cb); }

    // Process a pushbuffer (sequence of 32-bit words)
    // Returns the number of words consumed, or 0 on error.
    size_t Process(std::span<const u32> words);

    // Process a single GPFifo entry (method + argument)
    // Returns true if the entry was valid.
    bool ProcessEntry(u32 method, u32 arg);

    // Get the engine type for a subchannel
    EngineType GetEngine(u32 subch) const;

    // Set engine type for a subchannel
    void SetEngine(u32 subch, EngineType type);

    // ── Static helpers ───────────────────────────────────
    static u32  MakeHeader(Mode mode, u32 method, u32 arg, u32 subch);
    static u32  GetMethod(u32 header);
    static u32  GetSubchannel(u32 header);
    static u32  GetArg(u32 header);
    static Mode GetMode(u32 header);

private:
    MethodCallback callback_;
    EngineType subch_engines_[8] = {Engine3D, Engine2D, EngineDma, EngineInline,
                                     EngineCompute, Engine3D, Engine3D, Engine3D};
};

// ── GPFifo method handlers (defined inline for clarity) ─────
// Process GPFifo-specific commands like semaphore, syncpoint.
// These are NOT forwarded to the engine callbacks.
struct GpfifoHandler {
    // Handle a GPFifo method. Returns true if handled.
    static bool Handle(u32 method, u32 arg);
};

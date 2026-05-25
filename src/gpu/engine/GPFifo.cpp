#include "gpu/engine/GPFifo.h"

// ── Static helpers ─────────────────────────────────────────
u32 GPFifo::MakeHeader(Mode mode, u32 method, u32 arg, u32 subch) {
    return (method & 0x1FFF) | ((subch & 0x7) << 13) | ((arg & 0x1FFF) << 16) | ((mode & 0x7) << 29);
}

u32 GPFifo::GetMethod(u32 header)   { return header & 0x1FFF; }
u32 GPFifo::GetSubchannel(u32 hdr) { return (hdr >> 13) & 0x7; }
u32 GPFifo::GetArg(u32 header)     { return (header >> 16) & 0x1FFF; }
GPFifo::Mode GPFifo::GetMode(u32 header) {
    return static_cast<Mode>((header >> 29) & 0x7);
}

// ── Constructor ─────────────────────────────────────────────
GPFifo::GPFifo() {}

// ── Engine type ─────────────────────────────────────────────
GPFifo::EngineType GPFifo::GetEngine(u32 subch) const {
    if (subch < 8) return subch_engines_[subch];
    return Engine3D;
}

void GPFifo::SetEngine(u32 subch, EngineType type) {
    if (subch < 8) subch_engines_[subch] = type;
}

// ── Process pushbuffer ─────────────────────────────────────
size_t GPFifo::Process(std::span<const u32> words) {
    size_t pos = 0;

    while (pos < words.size()) {
        u32 header = words[pos];
        Mode mode = GetMode(header);
        u32 method = GetMethod(header);
        u32 arg = GetArg(header);
        u32 subch = GetSubchannel(header);

        // Check for GPFifo-class command (subchannel 0 = host channel)
        if (subch == 0 && GpfifoHandler::Handle(method, arg)) {
            pos++;
            continue;
        }

        switch (mode) {
        case Increasing:
            // method, method+1, method+2, ..., method+arg-1
            if (pos + 1 + arg > words.size()) return pos;
            for (u32 i = 0; i < arg; i++) {
                if (callback_) callback_(subch, method + i, words[pos + 1 + i]);
            }
            pos += 1 + arg;
            break;

        case NonIncreasing:
            // Same method, arg times
            if (pos + 1 + arg > words.size()) return pos;
            for (u32 i = 0; i < arg; i++) {
                if (callback_) callback_(subch, method, words[pos + 1 + i]);
            }
            pos += 1 + arg;
            break;

        case Inline:
            // Method + data packed in the same word (arg = inline data)
            // The arg field IS the data word, no extra words follow
            if (callback_) callback_(subch, method, arg);
            pos++;
            break;

        case IncreaseOnce:
            // method, method+1 (only 2 total)
            if (pos + 2 > words.size()) return pos;
            if (callback_) callback_(subch, method, words[pos + 1]);
            pos += 2;
            break;

        default:
            LOG_WARN("Unknown GPFifo mode %u at word %zu (method=0x%x)", (u32)mode, pos, method);
            pos++;
            break;
        }
    }

    return pos;
}

// ── Process a single entry ─────────────────────────────────
bool GPFifo::ProcessEntry(u32 method, u32 arg) {
    // Treat as Inline mode (method + data combined)
    if (callback_) callback_(0, method, arg);
    return true;
}

// ── GPFifo handler ──────────────────────────────────────────
bool GpfifoHandler::Handle(u32 method, u32 arg) {
    switch (static_cast<GpfifoMethod>(method)) {
    case GpfifoMethod::SemaphoreOffset:
    case GpfifoMethod::SemaphorePayload:
    case GpfifoMethod::Semaphore:
        LOG_TRACE("GPFifo: semaphore method=0x%x arg=0x%x", method, arg);
        return true;

    case GpfifoMethod::SetReference:
        LOG_TRACE("GPFifo: SetReference=0x%x", arg);
        return true;

    case GpfifoMethod::SyncpointPayload:
    case GpfifoMethod::Syncpoint:
        LOG_TRACE("GPFifo: syncpoint method=0x%x arg=0x%x", method, arg);
        return true;

    default:
        return false;  // Not a GPFifo command — forward to engine
    }
}

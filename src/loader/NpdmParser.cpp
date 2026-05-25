#include "loader/NpdmParser.h"

#include "common/Log.h"
#include <cstring>

// ── NPDM magic ──────────────────────────────────────────────
constexpr u32 NPDM_MAGIC = 0x304D4450;  // "MDP0" (little-endian: "PDM0")

// ── Parse ───────────────────────────────────────────────────
Result NpdmParser::Parse(std::span<const u8> buffer, NpdmInfo& info) {
    info = Default();

    if (buffer.size() < 0x40) {
        LOG_WARN("NPDM too small (%zu bytes), using defaults", buffer.size());
        return Result::Success;
    }

    u32 magic;
    std::memcpy(&magic, buffer.data(), 4);

    if (magic != NPDM_MAGIC) {
        LOG_WARN("NPDM magic mismatch (0x%08x), using defaults", magic);
        return Result::Success;
    }

    LOG_DEBUG("NPDM size: %zu bytes", buffer.size());

    // Phase 1: minimal parse — only extract what homebrew needs
    // Full NPDM parsing will be implemented in Phase 6
    return Result::Success;
}

// ── Default (for homebrew without NPDM) ─────────────────────
NpdmInfo NpdmParser::Default() {
    NpdmInfo info;
    info.entry_point = 0x40000000;
    info.stack_size  = 0x100000;    // 1 MiB
    info.aslr_base   = 0x40000000;
    info.aslr_size   = 0x40000000;  // 1 GiB
    info.heap_min    = 0x80000000;
    info.heap_max    = 0xE0000000;
    info.title_name  = "Homebrew";
    return info;
}

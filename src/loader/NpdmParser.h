#pragma once

#include "common/Types.h"
#include <string>
#include <span>

// ── NPDM (Nintendo Process Definition Meta) Parser ──────────
// Describes the process layout: entry point, stack size,
// ASLR parameters, kernel capabilities.

struct NpdmInfo {
    u64 entry_point = 0;
    u64 stack_size  = 0x100000;  // Default 1 MiB
    u64 aslr_base   = 0x40000000;
    u64 aslr_size   = 0x40000000;  // 1 GiB
    u64 heap_min    = 0x80000000;
    u64 heap_max    = 0xE0000000;
    std::string title_name;
};

class NpdmParser {
public:
    // Parse an NPDM binary blob.
    static Result Parse(std::span<const u8> buffer, NpdmInfo& info);

    // Return a default NpdmInfo for homebrew (no NPDM).
    static NpdmInfo Default();
};

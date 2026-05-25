#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include <span>
#include <vector>

class NativeExec {
public:
    static constexpr u32 SVC_MASK    = 0xFF00001F;
    static constexpr u32 SVC_PATTERN = 0xD4000001;
    static constexpr u32 BRK_BASE    = 0xD4200000;
    static constexpr u32 BRK_TAG_BASE = 0x1000;
    static constexpr u32 MAX_SVC_ID  = 0x100;

    static Result PatchSVCs(u8* code, u64 size,
                            std::vector<std::pair<u32, u32>>& out_map);

    // Jump to guest code. Addresses must be ABSOLUTE host VAs.
    static void RunGuest(u64 abs_entry_point, u64 abs_stack_top, u64 tls_base);
};

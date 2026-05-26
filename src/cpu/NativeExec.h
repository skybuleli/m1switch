#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include "memory/Memory.h"
#include "cpu/ExceptionHandler.h"
#include <span>
#include <vector>

class NativeExec {
public:
    static constexpr u32 SVC_MASK    = 0xFFE0001F;
    static constexpr u32 SVC_PATTERN = 0xD4000001;
    static constexpr u32 BRK_BASE    = 0xD4200000;
    // BRK_TAG_BASE 在 ExceptionHandler.h 中定义
    static constexpr u32 MAX_SVC_ID  = 0x100;

    static Result PatchSVCs(u8* code, u64 size,
                            std::vector<std::pair<u32, u32>>& out_map);

    // Jump to guest code. Addresses must be ABSOLUTE host VAs.
    static void RunGuest(u64 abs_entry_point, u64 abs_stack_top, u64 tls_base,
                         u64 arg0 = 0, u64 arg1 = 0, u64 arg2 = 0);

    // ── Diagnostic variants for crash isolation ──────────
    static void RunGuest_Minimal(u64 abs_entry, u64 abs_stack);
    static void RunGuest_NoZero(u64 abs_entry, u64 abs_stack);
    static void RunGuest_FullDirect(u64 abs_entry, u64 abs_stack);
};

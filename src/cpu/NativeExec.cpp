#include "cpu/NativeExec.h"
#include <cstring>
#include <mach/mach.h>
#include <mach/mach_traps.h>

extern "C" void GuestTrampoline(u64 entry_point, u64 stack_top);

Result NativeExec::PatchSVCs(u8* code, u64 size,
                              std::vector<std::pair<u32, u32>>& out_map) {
    if (!code || size < 4 || (size % 4) != 0)
        return Result::InvalidArgument;
    out_map.clear();
    for (u64 offset = 0; offset + 4 <= size; offset += 4) {
        u32 inst;
        std::memcpy(&inst, code + offset, sizeof(inst));
        if ((inst & SVC_MASK) == SVC_PATTERN) {
            u32 svc_num = (inst >> 5) & 0xFFFF;
            if (svc_num >= MAX_SVC_ID) continue;
            u32 brk_tag = BRK_TAG_BASE + svc_num;
            u32 brk_inst = BRK_BASE | ((brk_tag & 0xFFFF) << 5);
            std::memcpy(code + offset, &brk_inst, sizeof(brk_inst));
            out_map.push_back({brk_tag, svc_num});
        }
    }
    __builtin___clear_cache(reinterpret_cast<char*>(code),
                            reinterpret_cast<char*>(code + size));
    return Result::Success;
}

// ── Jump to guest using thread state hijack ────────────────
// Sets PC and SP on the current thread, then yields.
// When the thread resumes, the kernel loads the new state.
void NativeExec::RunGuest(u64 abs_entry, u64 abs_stack, u64 tls_base) {
    LOG_INFO("Guest hijack: PC=0x%llx SP=0x%llx", abs_entry, abs_stack);

    // Read current thread state
    arm_unified_thread_state state;
    mach_msg_type_number_t sc = ARM_UNIFIED_THREAD_STATE_COUNT;
    kern_return_t kr = thread_get_state(mach_thread_self(),
                                         ARM_UNIFIED_THREAD_STATE,
                                         (thread_state_t)&state, &sc);
    if (kr != KERN_SUCCESS) {
        LOG_ERROR("thread_get_state failed: %d", kr);
        return;
    }

    // Set PC to guest entry, SP to guest stack
    state.ts_64.__pc = abs_entry;
    state.ts_64.__sp = abs_stack;

    // Save return PC in x19 (callee-saved register)
    state.ts_64.__x[19] = state.ts_64.__pc;

    // Clear frame pointer
    state.ts_64.__x[29] = 0;

    // Write back — kernel resumes at entry_point
    kr = thread_set_state(mach_thread_self(),
                           ARM_UNIFIED_THREAD_STATE,
                           (thread_state_t)&state, sc);
    if (kr != KERN_SUCCESS) {
        LOG_ERROR("thread_set_state failed: %d", kr);
        return;
    }

    // Thread switch to force the kernel to reload thread state
    // This should cause the thread to resume at the guest PC.
    // Use mach_msg timeout to trigger kernel re-schedule
    mach_msg_header_t msg = {};
    mach_msg(&msg, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
             0, sizeof(msg), MACH_PORT_NULL, 1, MACH_PORT_NULL);

    // If we reach here, the thread resumed with the NEW state
    // (guest code). The only way we return is if the guest
    // hits svcExitProcess and the handler redirects PC back.
    LOG_WARN("thread_set_state takeover did not take effect");
}

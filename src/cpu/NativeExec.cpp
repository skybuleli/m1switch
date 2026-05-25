#include "cpu/NativeExec.h"
#include <cstring>
#include <mach/mach.h>

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

void NativeExec::RunGuest(u64 abs_entry, u64 abs_stack, u64 tls_base) {
    LOG_INFO("Guest: PC=0x%llx SP=0x%llx", abs_entry, abs_stack);

    GuestTrampoline(abs_entry, abs_stack);

    LOG_ERROR("GuestTrampoline returned — should not happen");
}

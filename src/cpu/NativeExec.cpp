#include "cpu/NativeExec.h"
#include "cpu/ExceptionHandler.h"
#include "kernel/SvcTable.h"
#include <cstring>
#include <csetjmp>
#include <mach/mach.h>

extern "C" void GuestTrampoline(u64 entry_point, u64 stack_top,
                                u64 arg0, u64 arg1, u64 arg2);
extern "C" void GuestTrampoline_Minimal(u64 entry_point, u64 stack_top);
extern "C" void GuestTrampoline_NoZero(u64 entry_point, u64 stack_top);

thread_local sigjmp_buf g_guest_exit_jmp_buf;
thread_local bool g_guest_exit_jmp_valid = false;

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

            // 填充 BRK 缓存（信号处理函数使用，无需 vm_read）
            BrkCache_Add(reinterpret_cast<u64>(code) + offset, svc_num);
        }
    }
    __builtin___clear_cache(reinterpret_cast<char*>(code),
                            reinterpret_cast<char*>(code + size));
    return Result::Success;
}

void NativeExec::RunGuest(u64 abs_entry, u64 abs_stack, u64 tls_base,
                          u64 arg0, u64 arg1, u64 arg2) {
    LOG_INFO("Guest: PC=0x%llx SP=0x%llx", abs_entry, abs_stack);

    // 每线程信号栈 — 避免信号处理函数覆盖 guest 栈数据
    Result sr = SetupGuestSignalStack();
    if (Failed(sr)) {
        LOG_ERROR("Failed to setup guest signal stack, continuing anyway...");
    }

    g_guest_exit_jmp_valid = true;
    if (sigsetjmp(g_guest_exit_jmp_buf, 1) != 0) {
        LOG_INFO("Guest exited via SVC longjmp");
        return;
    }

    GuestTrampoline(abs_entry, abs_stack, arg0, arg1, arg2);

    LOG_ERROR("GuestTrampoline returned — should not happen");
    g_guest_exit_jmp_valid = false;
}

// ── Diagnostic variants for crash isolation ────────────────

// Test 1: minimal trampoline — just br x0, no SP/register changes
void NativeExec::RunGuest_Minimal(u64 abs_entry, u64 abs_stack) {
    LOG_INFO("DIAG_GUEST: Minimal (br x0 only) PC=0x%llx", abs_entry);
    GuestTrampoline_Minimal(abs_entry, abs_stack);
    LOG_ERROR("DIAG_GUEST: Minimal returned!");
}

// Test 2: SP change only, no register clearing
void NativeExec::RunGuest_NoZero(u64 abs_entry, u64 abs_stack) {
    LOG_INFO("DIAG_GUEST: NoZero (SP + br) PC=0x%llx SP=0x%llx", abs_entry, abs_stack);
    GuestTrampoline_NoZero(abs_entry, abs_stack);
    LOG_ERROR("DIAG_GUEST: NoZero returned!");
}

// Test 3: full trampoline but with direct br x0 instead of br x30
// (to test if mov x30, x0 + br x30 vs br x0 matters)
extern "C" void GuestTrampoline_FullDirect(u64 entry_point, u64 stack_top);
void NativeExec::RunGuest_FullDirect(u64 abs_entry, u64 abs_stack) {
    LOG_INFO("DIAG_GUEST: FullDirect PC=0x%llx SP=0x%llx", abs_entry, abs_stack);
    GuestTrampoline_FullDirect(abs_entry, abs_stack);
    LOG_ERROR("DIAG_GUEST: FullDirect returned!");
}

#include "cpu/NativeExec.h"
#include "cpu/ExceptionHandler.h"
#include "kernel/SvcTable.h"
#include "cpu/Debugger.h"
#include <cstring>
#include <csetjmp>
#include <mach/mach.h>
#include <sys/mman.h>
#include <pthread.h>

extern "C" void GuestTrampoline(u64 entry_point, u64 stack_top,
                                u64 arg0, u64 arg1, u64 arg2);

thread_local sigjmp_buf g_guest_exit_jmp_buf;
thread_local bool g_guest_exit_jmp_valid = false;

// mrs Xt, tpidrro_el0 指令编码
static constexpr u32 MRS_TPIDRRO_MASK = 0xFFFFFFE0;
static constexpr u32 MRS_TPIDRRO_PATTERN = 0xD53BD060;

// 固定 TLS 缓冲：在 mmap 可用地址中找一个能用单条 MOVZ 加载的地址
// MOVZ Xt, #imm16, LSL#(hw*16) 可以编码 4 种移位：0,16,32,48
// 尝试地址 0x100000000 (hw=2, imm16=1) 到 0xF00000000 (hw=2, imm16=15)
static u64 s_fixed_tls_addr = 0;
static constexpr u64 FIXED_TLS_SIZE = 0x2000;

static bool EnsureFixedTls() {
    if (s_fixed_tls_addr != 0) return true;

    // 尝试多个固定地址
    for (u64 try_addr = 0x100000000ULL; try_addr <= 0x100000000ULL * 15; try_addr += 0x100000000ULL) {
        munmap((void*)try_addr, FIXED_TLS_SIZE);
        void* result = mmap((void*)try_addr, FIXED_TLS_SIZE,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                           -1, 0);
        if (result != MAP_FAILED) {
            s_fixed_tls_addr = try_addr;
            // 复制当前线程 HOST TLS
            u64 host_tls;
            __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(host_tls));
            std::memcpy(result, reinterpret_cast<void*>(host_tls), FIXED_TLS_SIZE);
            LOG_INFO("Fixed TLS at 0x%llx (%zu KB)", s_fixed_tls_addr, FIXED_TLS_SIZE / 1024);
            return true;
        }
    }

    LOG_ERROR("EnsureFixedTls: could not allocate fixed TLS buffer");
    return false;
}

u64 GetFixedTlsAddr() { return s_fixed_tls_addr; }

// MOVZ Xt, #(addr>>32), LSL#32   — 加载 32 位对齐的地址
static u32 MakeFixedTlsLoad(u32 rd) {
    u32 imm16 = (u32)(s_fixed_tls_addr >> 32);  // hw=2, imm16 = high 16 bits
    return 0xD2800000 | (2 << 21) | (imm16 << 5) | rd;
}

Result NativeExec::PatchSVCs(u8* code, u64 size,
                              std::vector<std::pair<u32, u32>>& out_map) {
    if (!code || size < 4 || (size % 4) != 0)
        return Result::InvalidArgument;
    out_map.clear();

    bool has_fixed_tls = EnsureFixedTls();

    u32 tls_patch_count = 0;
    for (u64 offset = 0; offset + 4 <= size; offset += 4) {
        u32 inst;
        std::memcpy(&inst, code + offset, sizeof(inst));

        // Patch SVC → BRK
        if ((inst & SVC_MASK) == SVC_PATTERN) {
            u32 svc_num = (inst >> 5) & 0xFFFF;
            if (svc_num >= MAX_SVC_ID) continue;
            u32 brk_tag = BRK_TAG_BASE + svc_num;
            u32 brk_inst = BRK_BASE | ((brk_tag & 0xFFFF) << 5);
            std::memcpy(code + offset, &brk_inst, sizeof(brk_inst));
            out_map.push_back({brk_tag, svc_num});
            BrkCache_Add(reinterpret_cast<u64>(code) + offset, svc_num);
        }

        // Patch mrs Xt, tpidrro_el0 → movz Xt, #imm16, LSL#32
        // 加载固定 TLS 地址（所有线程共享，不受信号线程迁移影响）
        if (has_fixed_tls && (inst & MRS_TPIDRRO_MASK) == MRS_TPIDRRO_PATTERN) {
            u32 rd = inst & 0x1F;
            u32 new_inst = MakeFixedTlsLoad(rd);
            std::memcpy(code + offset, &new_inst, sizeof(new_inst));
            tls_patch_count++;
        }
    }
    if (tls_patch_count > 0) {
        LOG_INFO("Patched %u mrs tpidrro_el0 → fixed TLS 0x%llx",
                 tls_patch_count, s_fixed_tls_addr);
    }
    __builtin___clear_cache(reinterpret_cast<char*>(code),
                            reinterpret_cast<char*>(code + size));
    return Result::Success;
}

void NativeExec::RunGuest(u64 abs_entry, u64 abs_stack, u64 tls_base,
                          u64 arg0, u64 arg1, u64 arg2) {
    LOG_INFO("Guest: PC=0x%llx SP=0x%llx", abs_entry, abs_stack);

    // 如果入口在 MAP_JIT 区域内，切换为可执行模式
    // pthread_jit_write_protect_np(0) = 可写不可执行
    // pthread_jit_write_protect_np(1) = 可执行不可写
    if (g_jit_region_start != 0 && abs_entry >= g_jit_region_start &&
        abs_entry < g_jit_region_end) {
        pthread_jit_write_protect_np(1);
    }

    Result sr = SetupGuestSignalStack();
    if (Failed(sr)) {
        LOG_ERROR("Failed to setup guest signal stack, continuing...");
    }

    // 同步当前线程 HOST TLS → 固定 TLS 缓冲
    if (s_fixed_tls_addr != 0) {
        u64 host_tls;
        __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(host_tls));
        std::memcpy(reinterpret_cast<void*>(s_fixed_tls_addr),
                    reinterpret_cast<void*>(host_tls), FIXED_TLS_SIZE);
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

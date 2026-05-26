#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>

static int g_brk_hit = 0;

static void handler(int sig, siginfo_t *info, void *uap) {
    g_brk_hit++;
    ucontext_t *uc = (ucontext_t *)uap;
    printf("[handler] SIGTRAP caught! sig=%d code=%d PC=0x%llx\n",
           sig, info->si_code, uc->uc_mcontext->__ss.__pc);
    uc->uc_mcontext->__ss.__pc += 4;  // skip BRK
}

int main() {
    // Install SIGTRAP handler
    struct sigaction sa = {};
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTRAP, &sa, NULL);

    // Allocate executable memory
    mach_vm_address_t addr = 0x340000000ULL;
    mach_vm_allocate(mach_task_self(), &addr, 0x4000, VM_FLAGS_FIXED);

    // Write: MOV X0, #42; BRK #0x1000; MOV X0, #100; RET
    uint32_t code[] = {
        0xD2800540,  // MOV X0, #42
        0xD4214000,  // BRK #0xA000 (tag=0xA000)
        0xD2800C80,  // MOV X0, #100
        0xD65F03C0   // RET
    };
    memcpy((void*)(uintptr_t)addr, code, sizeof(code));

    // Make RX
    mach_vm_protect(mach_task_self(), addr, 0x4000, 0, VM_PROT_READ | VM_PROT_EXECUTE);

    printf("Executing test code at 0x%llx...\n", addr);

    typedef int (*fn_t)(void);
    fn_t f = (fn_t)(uintptr_t)addr;
    int result = f();

    printf("Result: %d (expected 100 if BRK was caught)\n", result);
    printf("BRK hits: %d (expected 1)\n", g_brk_hit);

    if (g_brk_hit == 1 && result == 100) {
        printf("SUCCESS: BRK generates SIGTRAP on this macOS\n");
        return 0;
    } else {
        printf("FAIL: BRK did not behave as expected\n");
        return 1;
    }
}

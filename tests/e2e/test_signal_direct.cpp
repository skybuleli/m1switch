// Direct test: install SIGTRAP handler, emit BRK, verify handler fires
#include <stdio.h>
#include <signal.h>
#include <mach/mach.h>
#include <sys/ucontext.h>
#include <unistd.h>

static volatile int g_fired = 0;
static u64 g_pc = 0;
static u32 g_inst = 0;

static void handler(int sig, siginfo_t* info, void* uap) {
    auto* uc = (ucontext_t*)uap;
    g_pc = uc->uc_mcontext->__ss.__pc;
    g_inst = *(u32*)(unsigned long)g_pc;
    g_fired = 1;
    uc->uc_mcontext->__ss.__pc = g_pc + 4;
}

int main() {
    printf("=== Signal direct test ===\n");

    struct sigaction sa = {};
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTRAP, &sa, NULL);

    printf("Installing SIGTRAP handler...\n");

    // Emit BRK on main thread
    printf("Emitting BRK #0x1001...\n");
    __asm__ volatile ("brk #0x1001\n" ::: "memory");
    printf("After BRK\n");

    if (g_fired) {
        printf("Handler fired! PC=0x%llx inst=0x%08x\n", g_pc, g_inst);
        u32 tag = (g_inst >> 5) & 0xFFFF;
        printf("BRK tag: 0x%x (SVC #%u)\n", tag, tag - 0x1000);
        printf("PASSED\n");
        return 0;
    }

    // Try on a separate thread
    printf("Trying on separate thread...\n");
    g_fired = 0;

    pthread_t t;
    pthread_create(&t, NULL, [](void*) -> void* {
        __asm__ volatile ("brk #0x1002\n" ::: "memory");
        return NULL;
    }, NULL);
    pthread_join(t, NULL);

    if (g_fired) {
        printf("Handler fired from separate thread! inst=0x%08x\n", g_inst);
        printf("PASSED\n");
        return 0;
    }

    printf("FAILED: handler never fired\n");
    return 1;
}

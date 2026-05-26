// Test: can we allocate memory, write code, make it RX, and execute it?
#include <stdio.h>
#include <string.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <mach/mach_init.h>

int main() {
    mach_vm_address_t addr = 0x340000000ULL;
    size_t size = 0x4000;
    
    // Try without MAP_JIT first (current approach)
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &addr, size, VM_FLAGS_FIXED);
    if (kr != KERN_SUCCESS) {
        printf("ALLOCATE FAILED (NO JIT): kr=%d KERN_NO_SPACE=%d\n", kr, KERN_NO_SPACE);
        // Free and retry
        mach_vm_deallocate(mach_task_self(), addr, size);
        kr = mach_vm_allocate(mach_task_self(), &addr, size, VM_FLAGS_FIXED);
        if (kr != KERN_SUCCESS) {
            printf("ALLOCATE FAILED AFTER FREE: kr=%d\n", kr);
            return 1;
        }
    }
    printf("Allocated at 0x%llx\n", addr);
    
    // Write a function that returns 42 (MOV X0, #42; RET)
    uint32_t code[] = {
        0xD2800540,  // MOV X0, #42
        0xD65F03C0   // RET
    };
    memcpy((void*)(uintptr_t)addr, code, sizeof(code));
    
    // Make RX
    kr = mach_vm_protect(mach_task_self(), addr, size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS) {
        printf("PROTECT TO RX FAILED: kr=%d\n", kr);
        return 1;
    }
    printf("Protect to RX: OK\n");
    
    // Try to execute
    typedef int (*func_t)(void);
    func_t f = (func_t)(uintptr_t)addr;
    int result = f();
    printf("EXECUTION RESULT: %d (expected 42)\n", result);
    
    if (result == 42) {
        printf("SUCCESS: non-MAP_JIT memory CAN be executed from\n");
        return 0;
    } else {
        printf("FAIL: got %d instead of 42\n", result);
        return 1;
    }
}

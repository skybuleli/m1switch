# ── Guest entry trampoline ──────────────────────────────────
# Jumps to guest code with a clean register state.
# Preserves SP (host stack) so the guest has valid stack.

.text
.align 2
.globl _GuestTrampoline

_GuestTrampoline:
    // Save entry point (x0) before we zero everything
    mov     x30, x0

    // Zero all GPRs except SP (x29 is FP, x30 is entry point)
    mov     x0,  #0
    mov     x1,  #0
    mov     x2,  #0
    mov     x3,  #0
    mov     x4,  #0
    mov     x5,  #0
    mov     x6,  #0
    mov     x7,  #0
    mov     x8,  #0
    mov     x9,  #0
    mov     x10, #0
    mov     x11, #0
    mov     x12, #0
    mov     x13, #0
    mov     x14, #0
    mov     x15, #0
    mov     x16, #0
    mov     x17, #0
    mov     x18, #0
    mov     x19, #0
    mov     x20, #0
    mov     x21, #0
    mov     x22, #0
    mov     x23, #0
    mov     x24, #0
    mov     x25, #0
    mov     x26, #0
    mov     x27, #0
    mov     x28, #0
    mov     x29, #0
    // x30 has entry point
    // SP is preserved from the host

    // Jump to guest entry point
    br      x30

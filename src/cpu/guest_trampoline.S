# ── Guest entry trampoline ──────────────────────────────────
# Clears all registers and jumps to guest code.
# Arguments (C ABI): x0 = entry_point, x1 = stack_top (unused)

.text
.align 2
.globl _GuestTrampoline

_GuestTrampoline:
    // Save entry point in x30 (will be zeroed later but we use it first)
    mov     x30, x0

    // Zero all general-purpose registers
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
    // x30 already has entry_point — don't zero it

    // Jump to guest entry point via x30
    br      x30

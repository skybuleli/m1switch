# ── Guest entry trampoline ──────────────────────────────────
# Sets up environment and jumps to guest code.
# Arguments: x0 = entry_point, x1 = stack_top

.text
.align 2
.globl _GuestTrampoline
.globl _GuestTrampoline_Minimal
.globl _GuestTrampoline_NoZero
.globl _GuestTrampoline_FullDirect

# ── Full trampoline (original) ─────────────────────────────
_GuestTrampoline:
    // Set stack pointer to the provided guest stack
    mov     sp, x1

    // Save entry point
    mov     x30, x0

    // Clear frame pointer
    mov     x29, #0

    // Zero all general-purpose registers except x30 (entry)
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

    // Jump to guest entry point
    br      x30

# ── Minimal trampoline (no SP change, no register zeroing) ──
# Just jumps directly to guest code.
_GuestTrampoline_Minimal:
    br      x0

# ── No-zero trampoline (SP change but no register clearing) ─
_GuestTrampoline_NoZero:
    mov     sp, x1
    mov     x30, x0
    mov     x29, #0
    br      x30

# ── Full direct trampoline (SP change + register zeroing, br x0 not x30)
_GuestTrampoline_FullDirect:
    mov     sp, x1
    mov     x29, #0
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
    br      x0

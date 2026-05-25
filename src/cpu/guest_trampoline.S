# ── Guest entry trampoline ──────────────────────────────────

.text
.align 2
.globl _GuestTrampoline

_GuestTrampoline:
    // Set up guest stack
    mov     sp, x1

    // Clear frame pointer
    mov     x29, #0

    // Clear link register — guest should NOT return via ret.
    // If it does, it will crash cleanly (pc=0) instead of
    // jumping to an unaligned system address.
    mov     x30, #0

    // Jump to guest entry point
    br      x0

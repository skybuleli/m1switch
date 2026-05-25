# ── Guest entry trampoline ──────────────────────────────────
# Called from NativeExec::RunGuest.
# Switches to guest stack and jumps to guest code.
# The Mach exception handler is already installed on this thread.
# When the guest hits a BRK (ex-SVC), the handler will process
# it and return to the next guest instruction.
#
# Arguments (C ABI):
#   x0 = entry_point (guest PC)
#   x1 = stack_top   (guest SP)

.text
.align 2
.globl _GuestTrampoline

_GuestTrampoline:
    // Save return address (we'll need it when guest calls svcExitProcess)
    mov     x16, x30

    // Switch to guest stack
    mov     sp, x1

    // Clear frame pointer
    mov     x29, #0

    // Jump to guest
    br      x0

    // We never return here — guest runs until svcExitProcess.
    // When that happens, the exception handler redirects PC
    // to _GuestReturn, which is linked below.
    // (Not yet implemented in Phase 1 — will use x19 as return PC)

_GuestReturn:
    // Return from trampoline back to NativeExec::RunGuest
    ret

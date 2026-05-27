// ── L0: svcGetSystemTick + SvcSleepSystem ──────────────────
// SVC #0x1E: GetSystemTick() → x0 = tick
// SVC #0x49: SleepSystem(ns)  → x0 = result
// SVC #0x07: ExitProcess
// SVC #0x27: OutputDebugString(ptr, len)
//────────────────────────────────────────────────────────────

.section .text
.global _start
_start:
    b _entry
    .skip 0x10C
_entry:
    // ── 获取初始 tick ──────────────────────────────
    svc #0x1E                  // GetSystemTick
    mov x20, x0                // x20 = tick0

    // ── 睡眠 10ms ──────────────────────────────────
    mov x0, #0x9680            // 10,000,000 ns = 10ms (低16位)
    movk x0, #0x98, lsl #16   // 高16位
    svc #0x49                  // SleepSystem
    cbnz x0, _fail

    // ── 获取结束 tick ──────────────────────────────
    svc #0x1E                  // GetSystemTick
    mov x21, x0                // x21 = tick1

    // ── 验证 tick 前进 ─────────────────────────────
    cmp x21, x20
    b.ls _fail                 // tick1 <= tick0 → 失败

    // ── 睡眠 0ns（yield）───────────────────────────
    mov x0, #0
    svc #0x49                  // SleepSystem(0)
    cbnz x0, _fail

    // ── 再次验证 ───────────────────────────────────
    svc #0x1E
    cmp x0, x21
    b.ls _fail                 // tick 应该继续前进

    // ── 通过 ────────────────────────────────────────
    adr x0, _pass_str
    mov x1, #37
    svc #0x27
    mov x0, #0
    svc #0x07

_fail:
    adr x0, _fail_str
    mov x1, #37
    svc #0x27
    mov x0, #1
    svc #0x07

.align 4
_pass_str:
    .ascii "TEST_PASS: svc_sleep [asm]\n"
_fail_str:
    .ascii "TEST_FAIL: svc_sleep [asm]\n"

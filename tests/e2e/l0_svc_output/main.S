// ── L0: svcOutputDebugString ──────────────────────────────
// SVC #0x27: OutputDebugString(ptr, len) → 写入调试输出
// SVC #0x07: ExitProcess
//────────────────────────────────────────────────────────────

.section .text
.global _start
_start:
    b _entry
    .skip 0x10C
_entry:
    // ── 输出多行文本 ────────────────────────────────
    adr x0, _line1
    mov x1, #30
    svc #0x27

    adr x0, _line2
    mov x1, #31
    svc #0x27

    adr x0, _line3
    mov x1, #33
    svc #0x27

    adr x0, _pass_str
    mov x1, #38
    svc #0x27

    // ── 通过 ────────────────────────────────────────
    mov x0, #0
    svc #0x07

.align 4
_line1:
    .ascii "HELLO from M1Switch test NRO\n"
_line2:
    .ascii "LINE: the quick brown fox...\n"
_line3:
    .ascii "LINE: ...jumps over the lazy dog\n"
_pass_str:
    .ascii "TEST_PASS: svc_output [asm]\n"

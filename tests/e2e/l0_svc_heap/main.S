// ── L0: svcSetHeapSize + 内存模式写入 ──────────────────────
// SVC #0x00: SetHeapSize(size) → x0=result, x1=heap_addr
// SVC #0x07: ExitProcess
// SVC #0x27: OutputDebugString(ptr, len)
//────────────────────────────────────────────────────────────

.section .text
.global _start
_start:
    b _entry
    .skip 0x10C
_entry:
    // ── 调用 SetHeapSize ────────────────────────────
    mov x0, #0x100000          // size = 1MB
    svc #0x00
    // x0 = result, x1 = heap_addr
    cbnz x0, _fail             // 如果失败则跳转到_fail
    mov x20, x1                // x20 = heap_addr

    // ── 写入模式 0xAA ───────────────────────────────
    mov x21, #0x1000           // 写入 4KB 用于验证
    mov w22, #0xAA
    mov x0, x20                // 目标地址 = heap
    mov x1, #0x1000            // 大小
    mov w2, w22                // 模式
    bl _memset                 // 填充
    // 验证前 16 字节
    ldr x0, [x20]
    ldr x1, [x20, #8]
    mov x2, #0xAAAAAAAAAAAAAAAA
    cmp x0, x2
    b.ne _fail
    cmp x1, x2
    b.ne _fail

    // ── 写入模式 0x55 ───────────────────────────────
    mov w22, #0x55
    mov x0, x20
    mov x1, #0x1000
    mov w2, w22
    bl _memset
    ldr x0, [x20]
    mov x2, #0x5555555555555555
    cmp x0, x2
    b.ne _fail

    // ── 通过 → 输出 TEST_PASS ───────────────────────
    adr x0, _pass_str
    mov x1, #35                // 字符串长度
    svc #0x27                  // OutputDebugString
    mov x0, #0
    svc #0x07                  // ExitProcess(0)

_fail:
    adr x0, _fail_str
    mov x1, #35
    svc #0x27                  // OutputDebugString
    mov x0, #1
    svc #0x07                  // ExitProcess(1)

// ── 简单的 memset ───────────────────────────────────────
// x0 = addr, x1 = size, w2 = byte value
_memset:
    cmp x1, #0
    b.eq _memset_done
    strb w2, [x0]
    add x0, x0, #1
    sub x1, x1, #1
    b _memset
_memset_done:
    ret

// ── 字符串常量 ──────────────────────────────────────────
.align 4
_pass_str:
    .ascii "TEST_PASS: svc_heap [asm]\n"
_fail_str:
    .ascii "TEST_FAIL: svc_heap [asm]\n"

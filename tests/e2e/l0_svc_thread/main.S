// ── L0: svcCreateThread + StartThread + WaitSynchronization ──
// SVC #0x07: ExitProcess
// SVC #0x08: CreateThread(entry, arg, stack, prio, cpuid)
//            x1=entry (guest-relative), x2=arg, x3=stack, x4=prio, x5=cpuid
//            返回: x0=result, x1=handle
// SVC #0x09: StartThread(handle) → x0=result
// SVC #0x18: WaitSynchronization(handles_ptr, num, timeout_ns)
//            x0=handles_ptr, x1=num, x2=timeout_ns
// SVC #0x0A: ExitThread
// SVC #0x16: CloseHandle(handle)
// SVC #0x27: OutputDebugString(ptr, len)
//────────────────────────────────────────────────────────────

.section .text
.global _start
_start:
    b _entry
    .skip 0x10C
_entry:
    // ── 设置栈（SP 指向栈顶 0xC0000000）────────────
    // 先调用 SetHeapSize 获取堆作为子线程栈空间
    // SetHeapSize(size) → x0=result, x1=heap_addr (host-absolute)
    mov x0, #0x200000          // size = 2MB
    svc #0x00
    cbnz x0, _fail

    // ── 计算子线程入口的 guest 地址 ────────────────
    // _thread_func 是 host 地址（~0x340000xxx）。
    // 我们需要 guest 地址（~0x40000xxx）。
    // guest_addr = host_addr - 0x300000000
    // guest_addr = host_addr - host_base(0x300000000)
    // 构造 host_base = 0x300000000
    mov x21, #0x300000000      // x21 = host base (MOVZ + LSL #32)
    // _thread_func 的 guest 地址
    adr x20, _thread_func      // x20 = host addr of _thread_func
    sub x20, x20, x21          // x20 = guest addr of _thread_func

    // ── 子线程栈 ─────────────────────────────────
    // heap_base(host) + 0x100000 = stack(host)
    // stack_guest = stack_host - host_base
    add x22, x1, #0x100000     // x22 = stack host addr
    sub x22, x22, x21          // x22 = stack guest addr

    // ── 共享变量：heap 开头（host addr）────────────
    // 子线程写入共享变量，主线程读取验证
    // 子线程拿到 arg=heap_host_addr，写入 0x42
    // 我们直接用 host addr 读写
    mov x23, x1                // x23 = heap base (host addr)

    // ── 创建子线程 ──────────────────────────────────
    // CreateThread(entry, arg, stack, prio, cpuid)
    // x1=entry(guest), x2=arg(host), x3=stack(guest), x4=prio, x5=cpuid
    mov x1, x20                // x1 = entry (guest)
    mov x2, x23                // x2 = arg = heap base
    mov x3, x22                // x3 = stack (guest)
    mov x4, #0x10              // x4 = priority
    mov x5, #-2                // x5 = cpuid
    svc #0x08                  // CreateThread
    cbnz x0, _fail
    mov w24, w1                // w24 = thread handle

    // ── 启动线程 ────────────────────────────────────
    mov x0, x24                // handle
    svc #0x09                  // StartThread
    cbnz x0, _fail

    // ── 等待子线程完成（轮询共享变量，最多约 3 秒）───
    mov x25, #0x4000000        // 轮询次数上限
_poll_loop:
    ldr w0, [x23]              // 读取 heap[0]
    cmp w0, #0x42
    b.eq _poll_done            // 子线程已写入
    subs x25, x25, #1
    b.ne _poll_loop
    b _fail                    // 超时
_poll_done:

    // ── 验证共享变量 ────────────────────────────────
    ldr w0, [x23]              // 再次读取 heap[0]
    cmp w0, #0x42              // 应该是 0x42
    b.ne _fail

    // ── 关闭句柄 ────────────────────────────────────
    mov x0, x24
    svc #0x16                  // CloseHandle

    // ── 通过 ────────────────────────────────────────
    adr x0, _pass_str
    mov x1, #38
    svc #0x27                  // OutputDebugString
    mov x0, #0
    svc #0x07                  // ExitProcess

_fail:
    adr x0, _fail_str
    mov x1, #38
    svc #0x27                  // OutputDebugString
    mov x0, #1
    svc #0x07                  // ExitProcess

// ── 子线程入口 ──────────────────────────────────────────
// x0 = arg = heap base (host addr)
_thread_func:
    mov w1, #0x42
    str w1, [x0]               // heap[0] = 0x42
    svc #0x0A                  // ExitThread
    b _thread_func             // 不应到达

// ── 字符串常量 ──────────────────────────────────────────
.align 4
_pass_str:
    .ascii "TEST_PASS: svc_thread [asm]\n"
_fail_str:
    .ascii "TEST_FAIL: svc_thread [asm]\n"

// ── L0: svcExitProcess — 纯汇编版本 ────────────────────────
// 不链接 libnx，完全绕过 crt0。只测试：
//  - NRO 加载器
//  - 原生 ARM64 执行
//  - SVC #0x26 (svcExitProcess)
//────────────────────────────────────────────────────────────

.section .text
.global _start
_start:
    // 跳过 NRO 文件头（0x10-0x10F），跳到 0x110
    b _entry
    .skip 0x10C  // 填充到 0x110
_entry:
    // svcExitProcess()
    mov x0, #0
    mov x1, #0
    // 注意: M1Switch 的 svcExitProcess 是 SVC #0x07（非官方编号）
    svc #0x07

    // 如果 svcExitProcess 返回（不应发生），无限循环
    b _start

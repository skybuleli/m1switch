.text
.global _start
_start:
    mov x0, #0
    mov w22, #0xAA
    mov x0, #0
    svc #0x07

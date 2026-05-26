# M1Switch Test NRO — ARM64 Assembly
# Compile: as -arch arm64 test_nro.s -o test_nro.o
#          ld -o test_nro.bin -r -image_base 0 -subtype pure_code test_nro.o
# Then wrap with gen_test_nro.py

.section __TEXT,__text
.globl _start
.align 4

_start:
    // svcSetHeapSize(0x100000)
    mov x0, #0x100000           // 1 MiB heap
    svc #0                      // SetHeapSize → x0 = heap base

    // Copy heap address
    mov x1, x0                  // x1 = heap addr (destination)

    // Write orange pixels to heap (test pattern)
    // Color: BGRA = 0xFF, 0x20, 0x20, 0xFF (orange)
    mov w2, #0x20FF
    movk w2, #0xFF20, lsl #16   // w2 = 0xFF2020FF (orange in BGRA)

    mov w3, #100                // 100 pixels

loop:
    str w2, [x1], #4            // write pixel, post-increment
    subs w3, w3, #1
    b.gt loop

    // svcExitProcess(0)
    mov x0, #0
    svc #7

    // Infinite loop (fallback)
    b .

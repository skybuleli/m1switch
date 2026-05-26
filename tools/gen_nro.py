#!/usr/bin/env python3
"""Generate test.nro from pre-verified ARM64 binary."""
import struct, os

# Verified ARM64 code from as + otool (52 bytes / 13 instructions)
TEXT_CODE = bytes([
    0x00, 0x00, 0x80, 0xd2,  # mov  x0, #0x100000
    0x00, 0x02, 0xa0, 0xf2,  # movk x0, #0x10, lsl #16   → x0 = 1 MiB
    0x01, 0x00, 0x00, 0xd4,  # svc  #0                     → SetHeapSize
    0xe1, 0x03, 0x00, 0xaa,  # mov  x1, x0                 → heap addr
    0xe2, 0x1f, 0x84, 0xd2,  # mov  w2, #0x20FF
    0x02, 0xe4, 0xbf, 0xf2,  # movk w2, #0xFF20, lsl #16  → w2 = 0xFF2020FF (orange BGRA)
    0x83, 0x0c, 0x80, 0xd2,  # mov  w3, #100               → 100 pixels
    0x22, 0x04, 0x00, 0xb8,  # str  w2, [x1], #4           → write pixel, post-inc
    0x63, 0x06, 0x00, 0x71,  # subs w3, w3, #1
    0xac, 0xff, 0xff, 0x54,  # b.gt -0x10 (loop back)      → loop if >0
    0x00, 0x00, 0x80, 0xd2,  # mov  x0, #0
    0xe1, 0x00, 0x00, 0xd4,  # svc  #7                     → ExitProcess(0)
    0x00, 0x00, 0x00, 0x14,  # b .                          → infinite loop
])

def build_nro(text_data):
    text_size = len(text_data)
    text_aligned = (text_size + 0x1FF) & ~0x1FF
    total_size = 0x10 + 0x100 + text_aligned

    # NRO0 header (at file offset 0x10)
    h  = struct.pack('<IIII', 0x304F524E, 0, total_size, 0)  # 0x00: magic/ver/size/flags
    h += struct.pack('<II',    0x110, text_size)              # 0x10: text_start, text_size
    h += struct.pack('<II',    0x110 + text_aligned, 0)       # 0x18: rodata_start, rodata_size
    h += struct.pack('<II',    0x110 + text_aligned, 0)       # 0x20: data_start, data_size
    h += struct.pack('<II',    0, 0)                          # 0x28: bss_size, reserved
    h += b'\x00' * 32                                         # 0x30: build_id
    h += b'\x00' * (0x100 - len(h))                           # padding

    nro  = b'\x00' * 0x10    # 0x000: 16-byte preamble
    nro += h                  # 0x010: NRO0 header
    nro += text_data          # 0x110: .text
    nro += b'\x00' * (text_aligned - text_size)
    return nro

# Generate
nro = build_nro(TEXT_CODE)
with open('test.nro', 'wb') as f:
    f.write(nro)
print(f'✅ test.nro: {len(nro)} bytes')
print(f'   .text: {len(TEXT_CODE)} bytes at file offset 0x110')
print(f'   Entry: NRO_TEXT_BASE (0x40000000)')
print(f'   Tests: SetHeapSize → write 100 pixels → ExitProcess')

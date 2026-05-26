#!/usr/bin/env python3
"""
M1Switch Test NRO Generator
生成一个最小可执行 NRO，用于验证模拟器核心通路。

测试内容:
  1. svcSetHeapSize     — 分配堆内存
  2. 向堆中写入颜色像素  — 验证内存写入
  3. svcExitProcess     — 验证 SVC 分发

生成: test.nro (约 1KB)
"""

import struct
import sys
import os

# ═══════════════════════════════════════════════════════════
# ARM64 机器码 (手工编码)
# ═══════════════════════════════════════════════════════════
# 所有地址使用相对于 .text 起始的偏移，
# NRO 加载器会将 .text 映射到 0x40000000

def encode_movz(rd, imm16, shift=0):
    """MOVZ rd, #imm16, lsl #shift"""
    assert 0 <= rd <= 31 and 0 <= imm16 < 0x10000 and shift in (0, 16, 32, 48)
    return 0xD2800000 | (imm16 << 5) | (shift // 16 << 21) | rd

def encode_movk(rd, imm16, shift=0):
    """MOVK rd, #imm16, lsl #shift"""
    assert 0 <= rd <= 31 and 0 <= imm16 < 0x10000 and shift in (0, 16, 32, 48)
    return 0xF2800000 | (imm16 << 5) | (shift // 16 << 21) | rd

def encode_mov(rd, imm64):
    """MOV rd, #imm64 (using MOVZ + MOVK sequences)"""
    words = []
    for i in range(4):
        chunk = (imm64 >> (16 * i)) & 0xFFFF
        if chunk != 0 or i == 0:
            if not words:
                words.append(encode_movz(rd, chunk, 16 * i))
            else:
                words.append(encode_movk(rd, chunk, 16 * i))
    return words if words else [encode_movz(rd, 0, 0)]

def encode_svc(n):
    """SVC #n"""
    assert 0 <= n <= 0xFFFF
    return 0xD4000001 | (n << 5)

def encode_b(target_offset):
    """B #offset (unconditional branch, PC-relative)"""
    # offset is in bytes, must be 4-byte aligned
    imm26 = (target_offset // 4) & 0x3FFFFFF
    return 0x14000000 | imm26

def encode_b_inf():
    """B . (infinite loop - branches to itself)"""
    return 0x14000000  # B #0

def encode_nop():
    return 0xD503201F

def encode_str_reg(rt, rn, offset=0):
    """STR rt, [rn, #offset] — 64-bit store"""
    assert 0 <= rt <= 31 and 0 <= rn <= 31
    assert offset >= 0 and offset < 0x1000 and offset % 8 == 0
    return 0xF9000000 | (rt) | ((offset // 8) << 10) | (rn << 5)

def encode_strb(rt, rn, offset=0):
    """STRB rt, [rn, #offset] — 8-bit store"""
    assert 0 <= rt <= 31 and 0 <= rn <= 31
    assert offset >= 0 and offset < 0x1000
    return 0x39000000 | (rt) | (offset << 10) | (rn << 5)

# ═══════════════════════════════════════════════════════════
# 程序逻辑
# ═══════════════════════════════════════════════════════════
# 
# 这段代码在 0x40000000 (.text 基址) 执行:
#
#   MOV X0, #0x100000        ; heap size = 1 MiB
#   SVC #0                   ; svcSetHeapSize → X0 = heap addr
#   MOV X1, X0               ; X1 = heap addr (destination)
#   MOV W2, #0xFF2020FF      ; pixel color: orange (BGRA)
#   MOV W3, #100             ; 100 pixels
# loop:
#   STRB W2, [X1], #4        ; write pixel, increment ptr
#   SUBS W3, W3, #1
#   B.GT loop
#   MOV X0, #0
#   SVC #7                   ; svcExitProcess(0)
#   B .                      ; infinite loop (fallback)

def build_test_program():
    """Build the test ARM64 program and return as bytes."""
    instrs = []
    
    # SVC arguments (convention: X0-X7 = args, X0 = return)
    # svcSetHeapSize(size) → heap_addr
    instrs.extend(encode_mov(0, 0x100000))   # X0 = 1 MiB
    instrs.append(encode_svc(0x00))          # SetHeapSize → X0 = heap base
    
    # X1 = heap address (copy from X0)
    # On return from SetHeapSize, X0 = heap_base
    # MOV X1, X0  → 0xAA0003E1
    instrs.append(0xAA0003E1)   
    
    # X2 = pixel color (orange: R=0xFF, G=0x20, B=0x20, A=0xFF)
    # In BGRA format expected by framebuffer: B=0x20, G=0x20, R=0xFF, A=0xFF
    # → 0xFF2020FF
    instrs.append(encode_movz(2, 0x20FF, 0))    # lower 16
    instrs.append(encode_movk(2, 0xFF20, 16))   # upper 16
    
    # X3 = pixel count
    instrs.append(encode_movz(3, 100, 0))
    
    # Loop: write 100 pixels (400 bytes)
    loop_start = len(instrs)  # remember loop start for branch targets
    
    # STR W2, [X1], #4  — this is STR (32-bit) with post-increment
    # Encoding: 0xB8000000 | W2(=2) | X1(=1)<<5 | 4<<10 | 1<<24 (post-index)
    instrs.append(0xB8000422)  # STR W2, [X1], #4
    
    # SUBS W3, W3, #1 → 0x71000663
    instrs.append(0x71000663)
    
    # B.GT loop → B.GT offset = (loop_start - (current + 1)) & 0x3FFFFFF
    b_gt_offset = (loop_start - (len(instrs) + 1)) * 4  # +1 because B.GT is 1 instr
    # B.GT encoding: 0x54000000 | (cond=11=C) | imm19
    # Actually, B.GT is a B.cond instruction: 0x54000000 | (cond << 0) | (imm19 << 5)
    # cond = 0xC (GT)
    imm19 = b_gt_offset // 4
    imm19 &= 0x7FFFF  # 19-bit signed
    instrs.append(0x5400000C | (imm19 << 5))  # B.GT loop
    
    # svcExitProcess(0)
    instrs.append(encode_movz(0, 0, 0))
    instrs.append(encode_svc(0x07))
    
    # Infinite loop
    instrs.append(encode_b_inf())
    
    # Pack into binary
    data = b''
    for instr in instrs:
        data += struct.pack('<I', instr & 0xFFFFFFFF)
    
    return data


# ═══════════════════════════════════════════════════════════
# NRO 容器打包
# ═══════════════════════════════════════════════════════════
# NRO 文件布局:
#   0x000:  16B  前导码 (B 指令跳过头)
#   0x010:  256B NRO0 头部
#   0x110:  .text 段 (对齐到 0x200)
#   ...        .rodata / .data / .bss

def build_nro(text_data):
    """Wrap program bytes in an NRO0 container."""
    # NRO0 header at file offset 0x10
    nro0_magic = 0x304F524E  # "NRO0"
    
    # Calculate sizes
    text_size = len(text_data)
    text_aligned = (text_size + 0x1FF) & ~0x1FF  # align to 0x200
    
    # Total file size (preamble + header + text)
    total_size = 0x10 + 0x100 + text_aligned
    
    # Build NRO0 header (starts at file offset 0x10)
    header = b''
    header += struct.pack('<IIII', nro0_magic, 0, total_size, 0)  # 0x00-0x0F
    header += struct.pack('<II', 0x110, text_size)                 # 0x10-0x17: text_start, text_size
    header += struct.pack('<II', 0x110 + text_aligned, 0)          # 0x18-0x1F: rodata_start, rodata_size
    header += struct.pack('<II', 0x110 + text_aligned, 0)          # 0x20-0x27: data_start, data_size
    header += struct.pack('<II', 0, 0)                              # 0x28-0x2F: bss_size, reserved
    header += b'\x00' * 32                                          # 0x30-0x4F: build_id (zeros)
    header += b'\x00' * (0x100 - len(header))                       # 0x50-0xFF: padding
    
    # Build file
    nro = b''
    
    # 0x000: Preamble (B instruction to skip to .text)
    # B target = (0x110 - 0x000) / 4 = 0x44
    # But wait, the preamble branches relative to itself.
    # From offset 0, B #0x44 jumps to 0x110 (the .text)
    # Actually, let's use nop as preamble (the entry point is at NRO_TEXT_BASE)
    # For our loader, entry = NRO_TEXT_BASE, so the first instruction executed
    # is the first byte of .text (not the preamble)
    nro += b'\x00' * 0x10  # 16-byte preamble (all zeros/undefined)
    
    # 0x010: NRO0 header
    nro += header
    
    # 0x110: .text section
    nro += text_data
    nro += b'\x00' * (text_aligned - text_size)
    
    return nro


# ═══════════════════════════════════════════════════════════
# 主流程
# ═══════════════════════════════════════════════════════════

def main():
    print("Building M1Switch test NRO...")
    
    # Build the ARM64 test program
    text_data = build_test_program()
    print(f"  .text: {len(text_data)} bytes ({len(text_data)//4} instructions)")
    
    # Disassemble for debugging
    print("\n--- Disassembly ---")
    for i in range(0, len(text_data), 4):
        instr = struct.unpack('<I', text_data[i:i+4])[0]
        print(f"  {i:04x}: {instr:08x}")
    
    # Build NRO
    nro = build_nro(text_data)
    print(f"\n  NRO total: {len(nro)} bytes")
    
    # Write to file
    output_path = "test.nro"
    with open(output_path, 'wb') as f:
        f.write(nro)
    print(f"  Written to: {output_path}")
    
    # Print hex dump for verification
    print("\n--- NRO Hex Dump (first 0x200 bytes) ---")
    for i in range(0, min(len(nro), 0x200), 16):
        hex_str = ' '.join(f'{b:02x}' for b in nro[i:i+16])
        print(f"  {i:04x}: {hex_str}")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())

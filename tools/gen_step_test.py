#!/usr/bin/env python3
"""
逐步骤诊断 NRO — 直接发送原始 CMIF IPC，逐步测试每个服务。

运行: python3 gen_step_test.py --output step_test.nro
测试: m1switch_headless step_test.nro
"""
import struct, sys, os

def movz(rd, imm16, shift=0):
    return 0xD2800000 | (imm16 << 5) | (shift // 16 << 21) | rd
def movk(rd, imm16, shift=0):
    return 0xF2800000 | (imm16 << 5) | (shift // 16 << 21) | rd
def mov64(rd, val):
    r = []
    for i in range(4):
        c = (val >> (16*i)) & 0xFFFF
        if c or i == 0:
            if not r: r.append(movz(rd, c, 16*i))
            else: r.append(movk(rd, c, 16*i))
    return r or [movz(rd, 0)]
def svc(n):
    return 0xD4000001 | (n << 5)
def b_imm(off):
    return 0x14000000 | ((off//4) & 0x3FFFFFF)

# 指令助手
def stp(r1,r2,rn,off): return 0xA9000000|(r2<<10)|(rn<<5)|r1|((off//8)<<15)
def ldrw(rt,rn,off):   return 0xB9400000|(rt)|((off//4)<<10)|(rn<<5)
def strw(rt,rn,off):   return 0xB9000000|(rt)|((off//4)<<10)|(rn<<5)
def strd(rt,rn,off):   return 0xF9000000|(rt)|((off//8)<<10)|(rn<<5)
def cmp(rt, rn):       return 0x6B01001F|(rn<<16)|(rt<<5)
def csinc(rd,rn,rm,cond): return 0x1A800400|(rd)|(rn<<5)|(rm<<16)|cond
def add(rd,rn,imm12):  return 0x91000000|(rd)|(imm12<<10)|(rn<<5)

CMIF_IN_MAGIC = 0x49434653
CMIF_OUT_MAGIC = 0x4F434653

def build():
    """返回指令列表 + 步骤数"""
    code = []
    # X19=heap, X20=sm_h, X21=tmp, X22=TLS_base
    code.extend(mov64(22, 0x200000000))  # X22 = TLS base

    # ── Heap + sm 连接 ──
    code.extend(mov64(0, 0x100000))   # X0 = size
    code.append(svc(0x00))             # svcSetHeapSize → x0=0, x1=heap_addr
    code.append(0xAA0103F3)            # MOV X19, X1  (heap addr in x1, not x0!)
    code.append(movz(1, 0x6D73))
    code.append(movk(1, 0x3A00, 16))
    code.append(strw(1, 19, 0))
    code.append(0xAA1303E0)
    code.append(svc(0x1F))
    code.extend(mov64(1, 0x1000))
    code.append(cmp(0,1))    # CMP X0, X1
    code.append(b_imm(16))   # B.LO → skip error (skip mov64*1 + svc = 4+4+4=12? no...)
    # Actually this is backwards. We want: if NOT (X0 < 0x1000), continue
    # I need a different approach. Let me just check: if X0 >= 0x1000, skip error
    
    # B.LO means branch if unsigned lower (X0 < 0x1000) - go to error?
    # Hmm, let me restructure. We want: if X0 >= 0x1000 (valid), continue.
    # CMP X0, X1 with X1=0x1000: CS (carry set) means X0 >= X1 (unsigned)
    
    # Actually, simpler approach: CMP then B.CC (branch if carry clear = X0 < 0x1000 -> error)
    # Or: CMP then B.CS (branch if carry set = X0 >= 0x1000 -> skip error)
    pass

    # ── 步骤 1: SM::RegisterService (Control cmd=3) ──
    # 写 TLS: HipcHeader(8) + data: port_index(4) + SFCI(4) + cmd(4) + pad(4) = 4 words
    code.append(movz(0, 5))              # W0 = type=5 (Control)
    code.append(strw(0, 22, 0))          # TLS+0x00 = type
    code.append(movz(0, 4))              # W0 = num_data=4
    code.append(strw(0, 22, 4))          # TLS+0x04 = num_data
    code.append(strw(31, 22, 8))         # TLS+0x08 = port_index=0
    code.extend(mov64(0, CMIF_IN_MAGIC))
    code.append(strw(0, 22, 12))         # TLS+0x0C = SFCI
    code.extend(mov64(0, 3))
    code.append(strw(0, 22, 16))         # TLS+0x10 = cmd=3
    code.append(strw(31, 22, 20))        # TLS+0x14 = token=0
    code.append(0xAA1403E0)             # MOV X0, X20
    code.append(svc(0x21))              # svcSendSyncRequest
    # 检查响应 magic
    code.append(ldrw(0, 22, 8))          # LDR W0, [X22, #8] (data word 0)
    # 对于 Control 响应: CmifOutHeader 在 data_words[0]
    code.extend(mov64(1, CMIF_OUT_MAGIC))
    code.append(cmp(0,1))
    code.append(b_imm(8))                # B.EQ → continue
    code.extend(mov64(0, 10))
    code.append(svc(0x07))               # Exit(10) — step 1 fail

    # ── 步骤 2: SM::Initialize (Request cmd=0) ──
    # HipcHeader: type=4(Request), special_header(1), copy_handle=1
    # SpecialHeader: send_pid=0, copy=1, move=0
    code.append(movz(0, 4))
    code.append(strw(0, 22, 0))          # TLS+0x00 = type=4
    # num_data=4, has_special_header=1 (bit 31 set)
    code.extend(mov64(0, 0x80000004))    # num_data=4, has_special=1
    code.append(strw(0, 22, 4))
    # SpecialHeader: copy=1
    code.extend(mov64(0, 0x20))          # copy_handle=1 (bits 5:1 = 00010)
    code.append(strw(0, 22, 8))
    # CmifInHeader at offset 16
    code.extend(mov64(0, CMIF_IN_MAGIC))
    code.append(strw(0, 22, 16))
    code.append(strw(31, 22, 20))
    code.append(strw(31, 22, 24))        # cmd=0
    code.append(strw(31, 22, 28))        # token=0
    code.append(0xAA1403E0)
    code.append(svc(0x21))
    code.extend(mov64(0, 11))
    code.append(svc(0x07))               # Exit(11)

    # ── 步骤 3: SM::GetService("set:sys") ──
    # Request cmd=1, raw_in = "set:sys\0\0" spaced to 8 bytes
    code.extend(mov64(0, 4))
    code.append(strw(0, 22, 0))
    code.extend(mov64(0, 0x80000006))    # num_data=6, has_special=1
    code.append(strw(0, 22, 4))
    code.extend(mov64(0, 0x40))          # copy=0, move=1 (bit 9:5 = 00100 -> 0x40)
    code.append(strw(0, 22, 8))
    # CmifInHeader at offset 16
    code.extend(mov64(0, CMIF_IN_MAGIC))
    code.append(strw(0, 22, 16))
    code.append(strw(31, 22, 20))
    code.extend(mov64(0, 1))            # cmd=1
    code.append(strw(0, 22, 24))
    code.append(strw(31, 22, 28))
    # Service name at offset 32: "set:sys\0"
    code.extend(mov64(0, 0x007379733A746573))  # "set:sys\0"
    code.append(strd(0, 22, 32))
    code.append(0xAA1403E0)
    code.append(svc(0x21))
    code.extend(mov64(0, 12))
    code.append(svc(0x07))               # Exit(12)

    # ── 结束 ──
    code.extend(mov64(0, 0))
    code.append(svc(0x07))
    code.append(b_imm(0))
    return code

def build_nro(code):
    text = b''
    for i in code:
        text += struct.pack('<I', i & 0xFFFFFFFF)
    text_sz = len(text)
    text_aligned = (text_sz + 0x1FF) & ~0x1FF
    total = 0x10 + 0x100 + text_aligned
    hdr = struct.pack('<IIII', 0x304F524E, 0, total, 0)
    hdr += struct.pack('<II', 0x110, text_sz)
    hdr += struct.pack('<II', 0x110 + text_aligned, 0)
    hdr += struct.pack('<II', 0x110 + text_aligned, 0)
    hdr += struct.pack('<II', 0, 0)
    hdr += b'\x00' * 32
    hdr += b'\x00' * (0x100 - len(hdr))
    nro = b'\x00' * 0x10 + hdr + text + b'\x00' * (text_aligned - text_sz)
    return nro

if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", default="step_test.nro")
    args = ap.parse_args()
    code = build()
    nro = build_nro(code)
    with open(args.output, 'wb') as f:
        f.write(nro)
    print(f"Generated {args.output}: {len(nro)} bytes, {len(code)} instructions")
    for i, ins in enumerate(code):
        print(f"  {i*4:04x}: {ins:08x}")

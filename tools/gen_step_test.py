#!/usr/bin/env python3
"""
生成逐步骤诊断 NRO，直接测试每个 IPC 服务调用。

每个测试步骤：sm → set:sys → apm → appletOE
退出码: 0=成功, 1=失败, 2+ = 具体步骤
"""

import struct, sys, os

def movz(rd, imm16, shift=0):
    return 0xD2800000 | (imm16 << 5) | (shift // 16 << 21) | rd
def movk(rd, imm16, shift=0):
    return 0xF2800000 | (imm16 << 5) | (shift // 16 << 21) | rd
def mov64(rd, val):
    words = []
    for i in range(4):
        chunk = (val >> (16 * i)) & 0xFFFF
        if chunk != 0 or i == 0:
            if not words: words.append(movz(rd, chunk, 16 * i))
            else: words.append(movk(rd, chunk, 16 * i))
    return words if words else [movz(rd, 0)]
def svc(n):
    return 0xD4000001 | (n << 5)
def ret():
    return 0xD65F03C0
def b_imm(offset_words):
    return 0x14000000 | (offset_words & 0x3FFFFFF)
def cbz(rt, offset_words):
    return 0x34000000 | (rt) | ((offset_words & 0x7FFFF) << 5)
def cbnz(rt, offset_words):
    return 0x35000000 | (rt) | ((offset_words & 0x7FFFF) << 5)
def cmp_imm(rn, imm12):
    return 0x71000000 | (rn << 5) | (imm12 << 10)
def b_ne(offset_words):
    return 0x54000001 | ((offset_words & 0x7FFFF) << 5)
def b_eq(offset_words):
    return 0x54000000 | ((offset_words & 0x7FFFF) << 5)

# CMIF 请求构建 (写 TLS+0x00)
# struct: HipcHeader(8) + optional CmifInHeader(16)
CMIF_MAGIC = 0x49434653  # "SFCI"

def hipc_req_header(num_data=0, type=0):
    """生成 HipcHeader (8 bytes). type=4=Request, type=5=Control"""
    h = struct.pack('<H', type & 0xFFFF)  # type
    h += struct.pack('<H', 0)  # statics + buffers
    h += struct.pack('<I', num_data & 0x3FF)  # num_data_words
    return h

def cmif_in_header(cmd_id, token=0):
    return struct.pack('<IIII', CMIF_MAGIC, 0, cmd_id, token)

def build():
    code = []

    # ════════════════════════════════════════════
    # 寄存器分配:
    # X19 = heap base
    # X20 = sm session handle
    # X21 = temp service handle
    # X22 = step counter
    # ════════════════════════════════════════════

    # ── 1. svcSetHeapSize(0x100000) ──────────────
    code.extend(mov64(0, 0x100000))
    code.append(svc(0x00))
    code.append(0xAA0003F3)  # MOV X19, X0 (X19 = heap base)
    code.extend(mov64(22, 0))  # X22 = step counter

    # ── 2. svcConnectToNamedPort("sm:") ──────────
    # 写 "sm:\0" 到 heap
    code.append(movz(1, 0x6D73, 0))
    code.append(movk(1, 0x3A00, 16))
    code.append(0xB9000021 | (19 << 5))  # STR W1, [X19]
    code.append(0xAA1303E0)  # MOV X0, X19
    code.append(svc(0x1F))  # svcConnectToNamedPort → X0 = handle
    
    # 检查失败
    code.extend(mov64(1, 0x1000))
    code.append(0xEB01001F)  # CMP X0, X1
    code.append(b_lo(2))  # if X0 < 0x1000, skip error
    code.extend(mov64(0, 1))
    code.append(svc(0x07))  # svcExitProcess(1)
    code.append(0xAA0003F4)  # MOV X20, X0 (X20 = sm session)

    # ════════════════════════════════════════════
    # 步骤 1: SM::RegisterService (cmd=3 Control)
    # TLS+0:
    #   HipcHeader: type=5, num_data=6 (24 bytes)
    #   CmifInHeader: cmd_id=3
    # ════════════════════════════════════════════
    code.append(0xAA1303E1)  # MOV X1, X19 (use heap as TLS buffer)
    # Write HipcHeader
    code.extend(mov64(2, 0x05000000))  # type=5
    code.extend(mov64(3, 0x06000000))  # num_data=6
    code.append(0xA9000822)  # STP X2, X3, [X1]
    # Write CmifInHeader at offset 16 (SFCI + cmd=3)
    code.extend(mov64(2, CMIF_MAGIC))
    code.extend(mov64(3, 0x03000000))  # cmd_id=3, token=0
    code.append(0xA9002022 | (1 << 10))  # STP X2, X3, [X1, #16]
    # svcSendSyncRequest(sm_handle)
    code.append(0xAA1403E0)  # MOV X0, X20
    code.append(svc(0x21))
    code.extend(mov64(0, 999))
    code.append(svc(0x26))  # svcOutputDebugString("SM_REG")

    # ════════════════════════════════════════════
    # 步骤 2: SM::Initialize (cmd=0 Request)
    # Wait, first we need to set up the IPC properly
    # ════════════════════════════════════════════

    # ── svcExitProcess(0) ──────────────
    code.extend(mov64(0, 0))
    code.append(svc(0x07))
    code.append(b_imm(0))

    return code

if __name__ == "__main__":
    code = build()
    nro = build_nro(code)
    # ... write to file

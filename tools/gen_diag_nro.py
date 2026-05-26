#!/usr/bin/env python3
"""
生成用于诊断 NRO 运行通路的测试 ROM。

测试 1 (exit): svcExitProcess — 最小验证
测试 2 (heap):  svcSetHeapSize + 内存写入 + svcExitProcess
测试 3 (ipc):   svcConnectToNamedPort("sm:") + svcSendSyncRequest + svcCloseHandle
"""

import struct
import sys
import os


def movz(rd, imm16, shift=0):
    return 0xD2800000 | (imm16 << 5) | (shift // 16 << 21) | rd

def movk(rd, imm16, shift=0):
    return 0xF2800000 | (imm16 << 5) | (shift // 16 << 21) | rd

def mov64(rd, val):
    words = []
    for i in range(4):
        chunk = (val >> (16 * i)) & 0xFFFF
        if chunk != 0 or i == 0:
            if not words:
                words.append(movz(rd, chunk, 16 * i))
            else:
                words.append(movk(rd, chunk, 16 * i))
    return words if words else [movz(rd, 0, 0)]

def svc(n):
    return 0xD4000001 | (n << 5)

def nop():
    return 0xD503201F

def b_offset(offset):
    imm26 = (offset // 4) & 0x3FFFFFF
    return 0x14000000 | imm26

def b_inf():
    return 0x14000000

def str_x(rt, rn, offset=0):
    """STR Xt, [Xn, #offset] — 64-bit store, offset must be multiple of 8"""
    assert offset >= 0 and offset < 0x8000 and offset % 8 == 0
    return 0xF9000000 | (rt) | ((offset // 8) << 10) | (rn << 5)

def ldr_x(rt, rn, offset=0):
    """LDR Xt, [Xn, #offset]"""
    assert offset >= 0 and offset < 0x8000 and offset % 8 == 0
    return 0xF9400000 | (rt) | ((offset // 8) << 10) | (rn << 5)

def stp(x1, x2, xn, offset=0):
    """STP X1, X2, [Xn, #offset]"""
    assert offset >= 0 and offset < 0x200 and offset % 8 == 0
    return 0xA9000000 | (x2 << 10) | (xn << 5) | x1 | ((offset // 8) << 15)

def ldp(x1, x2, xn, offset=0):
    """LDP X1, X2, [Xn, #offset]"""
    assert offset >= 0 and offset < 0x200 and offset % 8 == 0
    return 0xA9400000 | (x2 << 10) | (xn << 5) | x1 | ((offset // 8) << 15)

def ret():
    return 0xD65F03C0


def build_ipc_test():
    """测试 SVC + IPC 通路。
    
    流程:
    1. svcSetHeapSize(0x100000) → 分配堆
    2. 在堆上写入 SVC 调用信息到 TLS+0x100 (IPC buffer)
    3. svcConnectToNamedPort("sm:\0") → 连接 SM 服务
    4. 如果连接成功 (X0 >= 0xD000), 保存句柄到 X20
    5. 写入 IPC 请求到用户缓冲区
    6. svcSendSyncRequest(handle) → 发送请求
    7. svcOutputDebugString("IPC test done")
    8. svcExitProcess(0)
    """
    code = []
    
    # ── 保存 LR 到 X19 ──────────────────────────────────────
    # (我们没有子程序调用，但保持兼容)
    
    # ── 1. svcSetHeapSize(0x100000) ─────────────────────────
    code.extend(mov64(0, 0x100000))       # X0 = 1 MiB
    code.append(svc(0x00))                 # svcSetHeapSize → X0 = heap base
    # X0 = heap base, 保存到 X20
    code.append(0xAA0003F4)                # MOV X20, X0
    
    # ── 2. svcConnectToNamedPort("sm:") ─────────────────────
    # 需要先在堆上写入服务名字符串
    # 服务名 "sm:\0" = 0x3A6D7300_00000073 (小端)
    # 我们把字符串写在堆的开头
    # "sm:\0\0\0\0\0" — 8 字节对齐
    code.extend(mov64(1, 0))               # X1 = 0 (临时清零)
    # 写入 "sm:\0" 到 heap
    # sm: = 0x73 0x6D 0x3A 0x00 (ASCII)
    # X2 = 0x003A6D73 (little-endian "sm:\0")
    code.append(movz(2, 0x6D73, 0))       # X2 = "sm" (2 bytes)
    code.append(movk(2, 0x003A, 16))      # X2 = "sm:\0" (4 bytes)
    # STR W2, [X20]
    code.append(0xB9000002 | (20 << 5))   # STR W2, [X20]
    
    # X0 = 指向字符串的指针 (heap base)
    code.append(0xAA1403E0)               # MOV X0, X20
    # svcConnectToNamedPort(name_ptr)
    code.append(svc(0x1F))                 # svcConnectToNamedPort → X0 = session handle
    
    # 检查返回值：如果 X0 >= 0xD000 说明失败
    code.extend(mov64(1, 0xD000))          # X1 = 错误阈值
    code.append(0xEB01001F)                # CMP X0, X1
    code.append(0x5400008C)                # B.GE +2 (跳到失败处理)
    # 成功：X0 = session handle, 保存到 X21
    code.append(0xAA0003F5)                # MOV X21, X0
    code.append(b_offset(4 * 4))           # B 跳过失败处理 (4条指令)
    
    # 失败处理：仍然继续（有些 SVC 可能返回非标准值）
    # X21 = 0 (失败标记)
    code.append(movz(21, 0, 0))
    
    # ── 3. 写 IPC 请求到 TLS+0x100 ────────────────────────
    # TLS 基址由模拟器设定 (0xFD000000)
    # IPC 请求格式: magic(0x49434246="IBCF") + cmd_id + ...
    # 简化为: 初始化命令 (cmd_id=0)
    
    # X0 = TLS_BASE + 0x100 = IPC 缓冲区地址
    # 我们用 svcGetInfo 先获得一些信息来验证
    # 或者直接用堆地址作为 IPC 缓冲区
    
    # ── 4. svcOutputDebugString ──────────────────────────────
    # 写 "IPC TEST" 到堆 + 0x100
    code.extend(mov64(1, 0))               # X1 = 0
    # "IPC TEST" = 8 字节
    # 用 svcOutputDebugString(ptr, len) 输出诊断信息
    # 先在堆+0x100 写字符串
    code.append(0x91040280)               # ADD X0, X20, #0x100 (X0 = heap+0x100)
    # 写 "IPC OK\0"
    # 'I'=0x49, 'P'=0x50, 'C'=0x43, ' '=0x20, 'O'=0x4F, 'K'=0x4B
    # little-endian: "IPC OK\0\0" = 0x004B4F20435049 等一下不对
    # 拆分: "IPC OK\0" → byte: 49 50 43 20 4F 4B 00 00
    # u64 = 0x00004B4F20435049
    code.extend(mov64(2, 0x4B4F20435049))  # "IPC OK\0"
    code.append(0xF9000002 | ((0x100 // 8) << 10) | (20 << 5))  # STR X2, [X20, #0x100]
    
    code.append(0x91040280)               # ADD X0, X20, #0x100
    code.extend(mov64(1, 6))              # X1 = 字符串长度
    code.append(svc(0x26))                 # svcOutputDebugString
    
    # ── 5. svcExitProcess(0) ────────────────────────────────
    code.append(movz(0, 0, 0))            # X0 = 0 (成功退出)
    code.append(svc(0x07))                 # svcExitProcess(0)
    code.append(b_inf())                   # 不应该到达这里
    
    return code


def build_minimal_test():
    """最小测试: 只调用 svcExitProcess(0)"""
    code = []
    code.append(movz(0, 0, 0))            # X0 = 0
    code.append(svc(0x07))                 # svcExitProcess(0)
    code.append(b_inf())
    return code


def build_heap_test():
    """堆测试: svcSetHeapSize + 写入 + svcExitProcess"""
    code = []
    code.extend(mov64(0, 0x100000))       # X0 = 1 MiB
    code.append(svc(0x00))                 # svcSetHeapSize
    # X0 = heap base, 保存
    code.append(0xAA0003F4)               # MOV X20, X0
    # 写入值到堆
    code.extend(mov64(1, 0xDEADBEEF))     # 标记值
    code.append(0xF9000000 | (0 << 10) | (20 << 5))  # STR X1, [X20]
    # 读回验证
    code.append(0xF9400000 | (0 << 10) | (20 << 5))  # LDR X0, [X20]
    # svcExitProcess(0)
    code.append(movz(0, 0, 0))
    code.append(svc(0x07))
    code.append(b_inf())
    return code


def build_nro(code_instrs, name="test"):
    """将 ARM64 指令打包成 NRO 文件"""
    text_data = b''
    for instr in code_instrs:
        text_data += struct.pack('<I', instr & 0xFFFFFFFF)
    
    text_size = len(text_data)
    text_aligned = (text_size + 0x1FF) & ~0x1FF
    
    total_size = 0x10 + 0x100 + text_aligned
    
    # NRO0 header
    header = b''
    header += struct.pack('<IIII', 0x304F524E, 0, total_size, 0)  # magic, version, size, flags
    header += struct.pack('<II', 0x110, text_size)                 # text_start, text_size
    header += struct.pack('<II', 0x110 + text_aligned, 0)          # rodata_start, rodata_size
    header += struct.pack('<II', 0x110 + text_aligned, 0)          # data_start, data_size
    header += struct.pack('<II', 0, 0)                              # bss_size, reserved
    header += b'\x00' * 32                                          # build_id
    header += b'\x00' * (0x100 - len(header))
    
    # Preamble
    nro = b'\x00' * 0x10
    nro += header
    nro += text_data
    nro += b'\x00' * (text_aligned - text_size)
    
    return nro


def main():
    import argparse
    parser = argparse.ArgumentParser(description="生成诊断用 NRO 测试 ROM")
    parser.add_argument("--test", choices=["exit", "heap", "ipc"], default="ipc",
                        help="测试类型: exit (最小), heap (堆), ipc (IPC通路)")
    parser.add_argument("--output", default=None, help="输出路径")
    args = parser.parse_args()
    
    tests = {
        "exit": build_minimal_test,
        "heap": build_heap_test,
        "ipc": build_ipc_test,
    }
    
    code = tests[args.test]()
    nro = build_nro(code, name=args.test)
    
    if args.output:
        out_path = args.output
    else:
        out_path = f"test_{args.test}.nro"
    
    with open(out_path, 'wb') as f:
        f.write(nro)
    
    print(f"生成 {args.test} 测试 NRO: {out_path} ({len(nro)} bytes, {len(code)} 指令)")
    
    # 反汇编
    print("\n--- 反汇编 ---")
    for i, instr in enumerate(code):
        print(f"  {i*4:04x}: {instr:08x}")


if __name__ == "__main__":
    main()
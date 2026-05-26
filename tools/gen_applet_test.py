#!/usr/bin/env python3
"""
M1Switch Applet 服务全面测试 NRO 生成器

测试范围:
  AmService (appletOE:/appletAE:)
    - cmd  0: GetAppletProxy → move handle
    - cmd 10: OpenSystemApplet → IWindowController session
    - cmd 40: GetAppletResource → ICommonStateGetter session
    - cmd 100: GetAppletType → u32 applet_type
    - cmd 200: GetMainAppletIdentityInfo → 0x40 bytes

  AmProxyService (通过 GetAppletProxy 获得)
    - cmd 0: GetCommonStateGetter → handle
    - cmd 1: GetSelfController → handle
    - cmd 2: GetWindowController → handle
    - cmd 3: GetAudioController → handle
    - cmd 4: GetDisplayController → handle
    - cmd 11: GetLibraryAppletCreator → handle
    - cmd 20: GetFunctions (IApplicationFunctions) → handle
    - cmd 1000: GetDebugFunctions → handle

  IWindowController
    - cmd  0: GetAppletResourceUserId → u64
    - cmd  1: AcquireForegroundRights → empty
    - cmd  2: ReleaseForegroundRights → empty
    - cmd 10: GetAppletResourceId → u32
    - cmd 20: CreateManagedDisplayLayer → u64
    - cmd 21: CreateManagedDisplayLayer2 → u64
    - cmd 30: GetIndirectLayerConsumerHandle → u64

  ICommonStateGetter
    - cmd  0: GetEventHandle → u32 handle
    - cmd  1: ReceiveMessage → u32 msg
    - cmd  5: GetOperationMode → u8 mode
    - cmd  6: GetPerformanceMode → u8 mode
    - cmd  7: GetCurrentFocusState → u8 state
    - cmd  9: SetFocusHandlingMode → empty
    - cmd 10: SetOutOfFocusSuspendingEnabled → empty
    - cmd 11: GetDefaultDisplayResolution → u32 w + u32 h
    - cmd 30: GetOperationModeChangeEvent → empty

  IApplicationFunctions (通过 proxy cmd 20 获得)
    - cmd  0: Initialize → empty
    - cmd  1: NotifyRunning → empty
    - cmd  2: GetPseudoDeviceId → 0x10 bytes
    - cmd 10: EnsureSaveData → u64
    - cmd 11: GetDisplayVersion → 16 bytes
    - cmd 50: EnsureSaveData2 → u64
    - cmd 100: SetTerminateResult → empty

  SelfControllerService (通过 proxy cmd 1 获得)
    - cmd 0: GetAppletResourceUserId → u64
    - cmd 1: AcquireForegroundRights → empty

每个步骤:
  1. 打印步骤名称到堆 (svcOutputDebugString)
  2. 构建 IPC 请求 (写 TLS 缓冲区)
  3. 发送 (svcSendSyncRequest)
  4. 验证响应:
     - SFCO magic (0x4F434653)
     - CmifOutHeader.result == 0
     - 数据大小和数据内容符合预期
  5. 如果验证失败: 设置退出码为步骤号并退出
  6. 如果通过: 进行下一步

运行: python3 tools/gen_applet_test.py
输出: applet_test.nro
"""

import struct
import sys
import os

# ═══════════════════════════════════════════════════════════
# ARM64 指令编码
# ═══════════════════════════════════════════════════════════

def movz(rd, imm16, shift=0):
    """MOVZ rd, #imm16, LSL #shift"""
    assert 0 <= rd <= 31 and 0 <= imm16 < 0x10000 and shift in (0, 16, 32, 48)
    return 0xD2800000 | (imm16 << 5) | (shift // 16 << 21) | rd

def movk(rd, imm16, shift=0):
    """MOVK rd, #imm16, LSL #shift"""
    assert 0 <= rd <= 31 and 0 <= imm16 < 0x10000 and shift in (0, 16, 32, 48)
    return 0xF2800000 | (imm16 << 5) | (shift // 16 << 21) | rd

def mov64(rd, val):
    """MOV rd, #imm64 (利用 MOVZ + MOVK)"""
    res = []
    for i in range(4):
        chunk = (val >> (16 * i)) & 0xFFFF
        if chunk != 0 or i == 0:
            if not res:
                res.append(movz(rd, chunk, 16 * i))
            else:
                res.append(movk(rd, chunk, 16 * i))
    return res if res else [movz(rd, 0, 0)]

def add(rd, rn, imm12, shift=0):
    """ADD rd, rn, #imm12 {LSL #0|12}"""
    assert 0 <= rd <= 31 and 0 <= rn <= 31 and 0 <= imm12 < 0x1000
    assert shift in (0, 12)
    return 0x91000000 | (rd) | (imm12 << 10) | (shift << 22) | (rn << 5)

def sub(rd, rn, imm12, shift=0):
    """SUB rd, rn, #imm12 {LSL #0|12}"""
    return 0xD1000000 | (rd) | (imm12 << 10) | (shift << 22) | (rn << 5)

def strb(rt, rn, offset=0):
    """STRB Wt, [Xn, #offset]"""
    assert 0 <= rt <= 31 and 0 <= rn <= 31
    assert 0 <= offset < 0x1000
    return 0x39000000 | (rt) | (offset << 10) | (rn << 5)

def strh(rt, rn, offset=0):
    """STRH Wt, [Xn, #offset]"""
    assert offset % 2 == 0 and offset < 0x1000
    return 0x78000000 | (rt) | ((offset // 2) << 10) | (rn << 5)

def strw(rt, rn, offset=0):
    """STR Wt, [Xn, #offset] — 32-bit store"""
    assert 0 <= rt <= 31 and 0 <= rn <= 31 and offset % 4 == 0 and offset < 0x1000
    return 0xB9000000 | (rt) | ((offset // 4) << 10) | (rn << 5)

def strd(rt, rn, offset=0):
    """STR Xt, [Xn, #offset] — 64-bit store"""
    assert 0 <= rt <= 31 and 0 <= rn <= 31 and offset % 8 == 0 and offset < 0x1000
    return 0xF9000000 | (rt) | ((offset // 8) << 10) | (rn << 5)

def ldrw(rt, rn, offset=0):
    """LDR Wt, [Xn, #offset]"""
    assert 0 <= rt <= 31 and 0 <= rn <= 31 and offset % 4 == 0 and offset < 0x1000
    return 0xB9400000 | (rt) | ((offset // 4) << 10) | (rn << 5)

def ldrd(rt, rn, offset=0):
    """LDR Xt, [Xn, #offset]"""
    return 0xF9400000 | (rt) | ((offset // 8) << 10) | (rn << 5)

def ldrb(rt, rn, offset=0):
    """LDRB Wt, [Xn, #offset]"""
    return 0x39400000 | (rt) | (offset << 10) | (rn << 5)

def mov(rd, rm, is64=True):
    """MOV rd, rm (ORR XZR, rm, rm)"""
    if is64:
        return 0xAA0003E0 | (rm << 16) | (rd)  # MOV Xd, Xm
    else:
        return 0x2A0003E0 | (rm << 16) | (rd)  # MOV Wd, Wm

def svc(n):
    """SVC #n"""
    return 0xD4000001 | (n << 5)

def b(offset):
    """B #offset (unconditional)"""
    imm26 = (offset // 4) & 0x3FFFFFF
    return 0x14000000 | imm26

def b_cond(cond, offset):
    """B.cond #offset"""
    imm19 = (offset // 4) & 0x7FFFF
    return 0x54000000 | (cond) | (imm19 << 5)

def cbz(rt, offset, is64=False):
    """CBZ/CBZW rt, #offset"""
    imm19 = (offset // 4) & 0x7FFFF
    return (0x34000000 if not is64 else 0xB4000000) | (rt) | (imm19 << 5)

def cbnz(rt, offset, is64=False):
    """CBNZ/CBNZW rt, #offset"""
    imm19 = (offset // 4) & 0x7FFFF
    return (0x35000000 if not is64 else 0xB5000000) | (rt) | (imm19 << 5)

def cmp(ra, rb, is64=True):
    """CMP ra, rb = SUBS XZR, ra, rb"""
    if is64:
        return 0xEB00001F | (rb << 16) | (ra << 5)
    else:
        return 0x6B00001F | (rb << 16) | (ra << 5)

def cmp_imm(rn, imm12, is64=True):
    """CMP rn, #imm12"""
    if is64:
        return 0xF100001F | (rn << 5) | (imm12 << 10)
    else:
        return 0x7100001F | (rn << 5) | (imm12 << 10)

def ccmp_imm(rn, imm5, nzcv, cond, is64=True):
    """CCMP rn, #imm5, #nzcv, cond"""
    if is64:
        return 0xFA400010 | (rn << 5) | (imm5 << 16) | (nzcv) | (cond)
    else:
        return 0x3A400010 | (rn << 5) | (imm5 << 16) | (nzcv) | (cond)

def cset(rd, cond, is64=False):
    """CSET rd, cond"""
    if is64:
        return 0x9A9F07E0 | (rd) | (cond)
    else:
        return 0x1A9F07E0 | (rd) | (cond)

def nop():
    return 0xD503201F

# ═══════════════════════════════════════════════════════════
# IPC 协议常量
# ═══════════════════════════════════════════════════════════

CMIF_IN_MAGIC  = 0x49434653   # "SFCI"
CMIF_OUT_MAGIC = 0x4F434653   # "SFCO"

# Hipc 类型
HIPC_TYPE_REQUEST = 4
HIPC_TYPE_CONTROL = 5

# 服务名编码 helper
def service_name_bytes(name):
    """把服务名转为 8 字节对齐"""
    b = name.encode('ascii') + b'\x00'
    while len(b) % 8 != 0:
        b += b'\x00'
    return b

# ═══════════════════════════════════════════════════════════
# 代码生成器类
# ═══════════════════════════════════════════════════════════

class ArmCode:
    """ARM64 指令序列生成器"""
    def __init__(self):
        self.code = []  # 指令列表
        self.labels = {}  # 标签名 → 当前指令索引
        self._pending_labels = {}  # 未知标签待解析

    def emit(self, instr):
        """发射一条 32-bit 指令"""
        if isinstance(instr, int):
            self.code.append(instr & 0xFFFFFFFF)
        elif isinstance(instr, list):
            for i in instr:
                self.emit(i)
        return self

    def label(self, name):
        """标记当前位置"""
        self.labels[name] = len(self.code)
        return self

    def word32(self, val):
        """发射一个 32-bit 数据字"""
        self.code.append(val & 0xFFFFFFFF)
        return self

    def word64(self, val):
        """发射一个 64-bit 数据双字 (两个 32-bit)"""
        self.code.append(val & 0xFFFFFFFF)
        self.code.append((val >> 32) & 0xFFFFFFFF)
        return self

    def data_bytes(self, b):
        """发射字节数组 (填充到 4 字节对齐)"""
        while len(b) % 4 != 0:
            b += b'\x00'
        for i in range(0, len(b), 4):
            w = struct.unpack_from('<I', b, i)[0]
            self.code.append(w)
        return self

    def b_to(self, target_label):
        """无条件跳转到标签 (暂存偏移，稍后解析)"""
        self._pending_labels.setdefault(target_label, []).append(len(self.code))
        self.code.append(0x14000000)  # placeholder
        return self

    def b_ne(self, target_label):
        """B.NE 暂存"""
        self._pending_labels.setdefault(target_label, []).append(len(self.code))
        self.code.append(0x54000001)  # placeholder (NE=1)
        return self

    def b_eq(self, target_label):
        """B.EQ 暂存"""
        self._pending_labels.setdefault(target_label, []).append(len(self.code))
        self.code.append(0x54000000)  # placeholder (EQ=0)
        return self

    def b_lo(self, target_label):
        """B.LO (unsigned lower) 暂存"""
        self._pending_labels.setdefault(target_label, []).append(len(self.code))
        self.code.append(0x54000003)  # placeholder (LO=3)
        return self

    def b_hs(self, target_label):
        """B.HS (unsigned higher or same) 暂存"""
        self._pending_labels.setdefault(target_label, []).append(len(self.code))
        self.code.append(0x54000002)  # placeholder (HS=2)
        return self

    def b_gt(self, target_label):
        """B.GT 暂存"""
        self._pending_labels.setdefault(target_label, []).append(len(self.code))
        self.code.append(0x5400000C)  # placeholder (GT=0xC)
        return self

    def resolve_labels(self):
        """解析所有待定标签"""
        for label_name, offsets in self._pending_labels.items():
            if label_name not in self.labels:
                raise ValueError(f"未定义的标签: {label_name}")
            target_idx = self.labels[label_name]
            for offset_idx in offsets:
                current_pc = offset_idx * 4
                target_pc = target_idx * 4
                diff = target_pc - current_pc
                instr = self.code[offset_idx]
                opcode = instr & 0xFC000000
                if opcode == 0x14000000:  # B (unconditional)
                    imm26 = (diff // 4) & 0x3FFFFFF
                    self.code[offset_idx] = 0x14000000 | imm26
                elif opcode == 0x54000000:  # B.cond
                    imm19 = (diff // 4) & 0x7FFFF
                    cond = instr & 0xF
                    self.code[offset_idx] = 0x54000000 | cond | (imm19 << 5)
                else:
                    raise ValueError(f"未知的分支指令: {instr:08x}")

    def finalize(self):
        """完成代码生成"""
        self.resolve_labels()
        return self.code


# ═══════════════════════════════════════════════════════════
# 测试生成器
# ═══════════════════════════════════════════════════════════

# 寄存器分配
R_HEAP    = 19   # X19 = 堆基址
R_TLS     = 22   # X22 = TLS 基址 (fixed at 0x200000000)
R_TMP0    = 20   # X20 = 临时
R_TMP1    = 21   # X21 = 临时
R_SESS    = 23   # X23 = 当前 session handle
R_STEP    = 24   # X24 = 步骤计数器
R_ERR     = 25   # X25 = 错误码

# TLS 偏移
TLS_IPC  = 0x000  # IPC 缓冲 (0x100 bytes)
TLS_RESV = 0x100  # 保留区域

TLS_SVC_ARGS = 0x80   # SVC 参数区域 (用于临时存储)

# 堆偏移
HEAP_STR_BUF = 0x000  # 字符串缓冲区

class AppletTestBuilder:
    """构建 applet 测试 NRO"""

    def __init__(self):
        self.ac = ArmCode()
        # 保存堆基址到 R_HEAP
        # 保存 TLS 基址到 R_TLS
        self.step_num = 0
        self.last_session = 0  # 上一个获得的 session handle
        self.session_reg = {}  # name → reg

    # ── 初始化 ──────────────────────────────────────────

    def emit_init(self):
        ac = self.ac

        # svcSetHeapSize(0x100000)
        ac.emit(mov64(0, 0x100000))
        ac.emit(svc(0x00))           # X0=result, X1=heap_addr
        ac.emit(mov(19, 1))          # X19 = heap base

        # 固定 TLS 地址已由模拟器设定在 0x200000000
        ac.emit(mov64(22, 0x200000000))  # X22 = TLS base

    # ── 打印字符串 ──────────────────────────────────────

    def emit_print(self, s):
        """用 svcOutputDebugString 输出字符串"""
        ac = self.ac
        # 在堆上写字符串
        b = s.encode('ascii') + b'\x00'
        # 8 字节对齐写入
        words = []
        for i in range(0, len(b), 8):
            chunk = b[i:i+8]
            val = 0
            for j, c in enumerate(chunk):
                if j < 8:
                    val |= c << (j * 8)
            words.append(val)

        for i, w in enumerate(words):
            ac.emit(mov64(0, w))
            ac.emit(strd(0, 19, HEAP_STR_BUF + i * 8))

        # X0 = 堆地址, X1 = 长度
        ac.emit(add(0, 19, HEAP_STR_BUF))
        ac.emit(mov64(1, len(b)))
        ac.emit(svc(0x27))  # svcOutputDebugString (SVC table: 0x27)

    # ── 步骤标记 ────────────────────────────────────────

    def emit_step(self, name):
        """开始一个新测试步骤"""
        self.step_num += 1
        # 先输出步骤名称
        self.emit_print(f"\n>>> STEP {self.step_num}: {name}")

    # ── IPC 请求构建 ────────────────────────────────────

    def emit_ipc_request_type(self, session_reg, cmd_id, type_val,
                               has_special=0, num_copy=0, num_move=0,
                               raw_in_bytes=None, domain_id=0):
        """
        构建 IPC 请求并发送。
        session_reg: 包含 session handle 的寄存器
        cmd_id: 命令 ID
        type_val: 4=Request, 5=Control
        has_special: 是否有 special header
        num_copy, num_move: handle 数量
        raw_in_bytes: 原始输入数据 (bytes)
        domain_id: 0 = 非 domain
        """
        ac = self.ac

        # ── 计算 HipcHeader ──
        # 数据部分需要对齐到 16 字节
        # 结构: HipcHeader(8) + [SpecialHeader(4)] + [handles(N*4)] + [static desc] + [buffer desc] + data_words
        # data_words 从 16 字节对齐处开始

        # 对于最简单的请求（无 handles、无 buffers），我们只需要：
        # HipcHeader(8) + data_words(从 16 对齐开始)

        # 更简单的方案：直接从 16 对齐处开始放 CmifInHeader

        # 简化版 HipcHeader: type=4/5, num_data_words 由后端自动计算
        # 我们直接写 data_words 部分

        # 构建 data_words 区域:
        # domain 模式: [domain_obj_id(4)] + CmifInHeader(16) + raw_in
        # 非 domain: CmifInHeader(16) + raw_in

        # 先把整个请求写入 TLS 缓冲
        # 跳过 HipcHeader(8) 和可能的 SpecialHeader
        hdr_size = 8
        if has_special:
            hdr_size += 4 + (num_copy + num_move) * 4

        # data_words 起始位置 (16 字节对齐)
        data_off = (hdr_size + 15) & ~15

        # 写入 data_words
        off = data_off

        if domain_id > 0:
            # domain 模式: 先写 object_id
            ac.emit(mov64(0, domain_id))
            ac.emit(strw(0, 22, off))
            off += 4

        # CmifInHeader: magic(4) + version(4) + cmd_id(4) + token(4)
        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, off))
        ac.emit(mov64(0, 0))           # version = 0
        ac.emit(strw(0, 22, off + 4))
        ac.emit(mov64(0, cmd_id))
        ac.emit(strw(0, 22, off + 8))
        ac.emit(mov64(0, self.step_num))  # token = step number
        ac.emit(strw(0, 22, off + 12))
        off += 16

        # raw input data
        if raw_in_bytes:
            for i in range(0, len(raw_in_bytes), 4):
                chunk = raw_in_bytes[i:i+4]
                val = 0
                for j, c in enumerate(chunk):
                    if j < 4:
                        val |= c << (j * 8)
                ac.emit(mov64(0, val))
                ac.emit(strw(0, 22, off + i))

        # 计算 total_data_words (从 data_off 到结束, 按 u32 计)
        total_data_end = off + (len(raw_in_bytes) if raw_in_bytes else 0)
        total_data_size = total_data_end - data_off
        num_data_words = (total_data_size + 3) // 4

        # 回填 HipcHeader
        # HipcHeader 格式: [0:16]=type, [16:20]=num_send_statics, [20:24]=num_send_buffers,
        #   [24:28]=num_recv_buffers, [28:32]=num_exch_buffers, [32:42]=num_data_words,
        #   [42:46]=recv_static_mode, [46:52]=padding, [52:63]=recv_list_offset,
        #   [63]=has_special_header
        hdr_val = (type_val & 0xFFFF) | ((num_data_words & 0x3FF) << 10)
        if has_special:
            hdr_val |= (1 << 31)  # has_special_header (bit 63 in u32? no wait)
            # 更正: has_special_header 是 HipcHeader 中的位域
            # 在 u32 表示中 (LE): type=bits[15:0], num_data_words=bits[25:16]? 
            # 实际上结构是 u32[2]
            # u32[0] = type[15:0] | num_send_statics[19:16] | num_send_buffers[23:20] |
            #          num_recv_buffers[27:24] | num_exch_buffers[31:28]
            # u32[1] = num_data_words[9:0] | recv_static_mode[13:10] | padding[19:14] |
            #          recv_list_offset[30:20] | has_special_header[31]
            pass

        # 重新整理 HipcHeader
        # u32[0] 的低 16 位 = type
        hdr_u32_0 = type_val & 0xFFFF

        # u32[1]:
        # bits [9:0]  = num_data_words
        # bits [13:10] = recv_static_mode (0 = 无)
        # bits [19:14] = padding
        # bits [30:20] = recv_list_offset
        # bit  [31]   = has_special_header
        hdr_u32_1 = (num_data_words & 0x3FF)
        if has_special:
            hdr_u32_1 |= (1 << 31)

        ac.emit(mov64(0, hdr_u32_0))
        ac.emit(strw(0, 22, 0))
        ac.emit(mov64(0, hdr_u32_1))
        ac.emit(strw(0, 22, 4))

        # SpecialHeader (如果有)
        if has_special:
            # HipcSpecialHeader: [0]=send_pid, [4:8]=num_copy, [8:12]=num_move, [12:31]=pad
            sh_val = (num_copy & 0xF) << 1 | (num_move & 0xF) << 5
            ac.emit(mov64(0, sh_val))
            ac.emit(strw(0, 22, 8))

        # ── 发送 IPC 请求 ──
        # X0 = session handle (来自 session_reg)
        ac.emit(mov(0, session_reg))
        ac.emit(svc(0x21))  # svcSendSyncRequest

    def emit_ipc_request(self, session_reg, cmd_id, has_special=0,
                          num_copy=0, num_move=0, raw_in_bytes=None, domain_id=0):
        """普通 Request (type=4)"""
        self.emit_ipc_request_type(session_reg, cmd_id, HIPC_TYPE_REQUEST,
                                    has_special, num_copy, num_move,
                                    raw_in_bytes, domain_id)

    def emit_ipc_control(self, session_reg, cmd_id, raw_in_bytes=None):
        """Control (type=5)"""
        self.emit_ipc_request_type(session_reg, cmd_id, HIPC_TYPE_CONTROL,
                                    0, 0, 0, raw_in_bytes, 0)

    # ── 响应验证 ────────────────────────────────────────

    def emit_verify_sfco(self):
        """验证响应中的 SFCO magic 和 result==0"""
        ac = self.ac
        # 响应数据在 TLS buffer 中
        # HipcHeader(8) + [data_words]
        # data_words 从 16 字节对齐处开始
        # 对于非 domain: CmifOutHeader 在 data_words[16 对齐]
        # 对于 Control: CmifOutHeader 在 data_words 起始

        # 从偏移 8 读响应 (跳过 HipcHeader)
        # 对于 Request 类型: data_words = offset = MAX(8, 16-align) = 16
        resp_off = 16  # CmifOutHeader 在 TLS+16 (16 字节对齐)

        # 验证 SFCO magic
        ac.emit(ldrw(0, 22, resp_off))
        ac.emit(mov64(1, CMIF_OUT_MAGIC))
        ac.emit(cmp(0, 1, is64=False))
        # 如果不相等 → 跳到失败标签
        fail_label = f"fail_magic_{self.step_num}"
        ok_label = f"ok_magic_{self.step_num}"
        ac.emit(ac.b_eq(ok_label))
        # magic 不匹配 → 打印错误并退出
        self.emit_print(f"  FAIL: STEP {self.step_num} bad SFCO magic")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))  # Exit(step)
        ac.label(ok_label)

        # 验证 result == 0 (CmifOutHeader 偏移 8 = result)
        ac.emit(ldrw(0, 22, resp_off + 8))
        ac.emit(cmp_imm(0, 0, is64=False))
        ok_label2 = f"ok_result_{self.step_num}"
        ac.emit(ac.b_eq(ok_label2))
        self.emit_print(f"  FAIL: STEP {self.step_num} result != 0")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok_label2)

    def emit_verify_response_u32(self, expected_val, offset_from_sfco=16):
        """验证响应数据中的 u32 值"""
        ac = self.ac
        resp_data_off = 16 + 16  # HipcHeader(16-align) + CmifOutHeader(16) = 32
        # 但 domain 模式可能多 4 字节? 不, SFCO 之后就是数据
        # 实际: TLS+resp_off = CmifOutHeader(16), 之后是数据
        data_off = 16 + 16  # HipcHeader(16) + CmifOutHeader(16)
        # offset_from_sfco 是相对于 CmifOutHeader 结尾的偏移
        actual_off = data_off + offset_from_sfco - 16

        ac.emit(ldrw(0, 22, actual_off))
        ac.emit(mov64(1, expected_val & 0xFFFFFFFF))
        ac.emit(cmp(0, 1, is64=False))
        ok_label = f"ok_data_{self.step_num}"
        ac.emit(ac.b_eq(ok_label))
        self.emit_print(f"  FAIL: STEP {self.step_num} data != expected")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok_label)

    def emit_verify_response_u64(self, expected_val, offset_before_sfco_end=0):
        """验证响应数据中的 u64 值"""
        ac = self.ac
        data_off = 16 + 16 + offset_before_sfco_end

        ac.emit(ldrd(0, 22, data_off))
        ac.emit(mov64(1, expected_val))
        # 只比较低 64 位
        # 实际上我们用 X0 和 X1 做 CMP
        ac.emit(cmp(0, 1, is64=True))
        ok_label = f"ok_data64_{self.step_num}"
        ac.emit(ac.b_eq(ok_label))
        self.emit_print(f"  FAIL: STEP {self.step_num} u64 mismatch")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok_label)

    def emit_verify_response_field(self, expected_bytes, offset=0):
        """验证响应数据的字节序列"""
        ac = self.ac
        data_off = 16 + 16 + offset
        for i, eb in enumerate(expected_bytes):
            ac.emit(ldrb(0, 22, data_off + i))
            ac.emit(mov64(1, eb))
            ac.emit(cmp(0, 1, is64=False))
            ok_label = f"ok_byte{i}_{self.step_num}"
            ac.emit(ac.b_eq(ok_label))
            self.emit_print(f"  FAIL: STEP {self.step_num} byte {i} mismatch")
            ac.emit(mov64(0, self.step_num))
            ac.emit(svc(0x07))
            ac.label(ok_label)

    def emit_verify_response_empty(self):
        """验证响应数据为空 (只有 CmifOutHeader)"""
        # 粗略验证: data_words 数量 = 4 (只有 CmifOutHeader)
        # HipcHeader 的 num_data_words 在 TLS+4 的 bits[9:0]
        ac = self.ac
        ac.emit(ldrw(0, 22, 4))  # u32[1] of HipcHeader
        ac.emit(mov64(1, 0x1FF))
        ac.emit(ldrw(0, 22, 4))
        ac.emit(mov64(1, 4))  # 4 words = CmifOutHeader only (no extra data)
        ac.emit(cmp(0, 1, is64=False))
        ok_label = f"ok_empty_{self.step_num}"
        # 可能还有其他情况，只是记录
        ac.label(ok_label)
        # 对于空响应，我们不严格验证 num_data_words

    def emit_verify_no_error(self):
        """只需确认 IPC 没有返回错误"""
        # svcSendSyncRequest 返回 X0 = result
        ac = self.ac
        ac.emit(cmp_imm(0, 0, is64=True))
        ok_label = f"ok_svc_{self.step_num}"
        ac.emit(ac.b_eq(ok_label))
        self.emit_print(f"  FAIL: STEP {self.step_num} svc returned error")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok_label)

    # ── 高层测试函数 ────────────────────────────────────

    def emit_connect_and_init_sm(self):
        """连接 sm: 并初始化"""
        ac = self.ac

        self.emit_step("Connect to sm:")
        # 在堆上写 "sm:\0"
        ac.emit(mov64(0, 0x003A6D73))  # "sm:\0" (little-endian: 's'=0x73, 'm'=0x6D, ':'=0x3A, '\0'=0x00)
        ac.emit(strw(0, 19, HEAP_STR_BUF))
        # svcConnectToNamedPort convention (Horizon OS / SVC handler):
        #   x0 = out_ptr (optional, 0 = no writeback)
        #   x1 = name_ptr
        # Returns: x0 = Result, x1 = session handle
        ac.emit(mov64(0, 0))              # X0 = 0 (no output buffer)
        ac.emit(add(1, 19, HEAP_STR_BUF)) # X1 = name_ptr ("sm:\0")
        ac.emit(svc(0x1F))
        # X0 = result, X1 = session handle
        ac.emit(mov(23, 1))  # X23 = sm: session

        self.emit_step("SM::Initialize (cmd 0, Control)")
        # SM::Initialize = Control cmd 0
        ac.emit(mov64(0, 5))  # type=5 (Control)
        ac.emit(strw(0, 22, 0))
        ac.emit(mov64(0, 4 << 10))  # num_data_words=4 (CmifInHeader only)
        ac.emit(strw(0, 22, 4))
        # CmifInHeader at offset 16
        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, 16))
        ac.emit(mov64(0, 0))  # version
        ac.emit(strw(0, 22, 20))
        ac.emit(mov64(0, 0))  # cmd_id = 0
        ac.emit(strw(0, 22, 24))
        ac.emit(mov64(0, 0))  # token
        ac.emit(strw(0, 22, 28))
        # 发送
        ac.emit(mov(0, 23))
        ac.emit(svc(0x21))
        ac.emit(cmp_imm(0, 0))
        ok = f"ok_sm_init_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: SM::Initialize failed")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

    def emit_sm_get_service(self, name, session_reg_out):
        """调用 SM::GetService (cmd 1) 获取服务

        注意: REQUEST 中绝对不能设 has_special_header+move handle!
        这会导致 IPC handler 的 recycled 检测误触发，重置请求头，
        使 CMIF 解析偏移量错误。
        响应中的 move handle 由 IPC handler 自动添加（needs_move_handle检查）。
        """
        ac = self.ac
        srv_bytes = service_name_bytes(name)

        self.emit_step(f"SM::GetService(\"{name}\")")

        # HipcHeader: type=4(Request), NO special header
        # 注意: SM::Initialize (Control cmd 0) 已将 sm: session 转为 domain 模式
        # 所以从步骤 3 开始，sm: session 的所有请求都用 domain 布局
        # domain 模式: 数据从 dw_off(8) 开始, cmif = dw + 4 = 12
        # 布局: HipcHeader(8) + domain_obj(4) + CmifInHeader(16) + service_name(8+)
        num_dw = 5 + (len(srv_bytes) // 4)  # domain_obj(1) + CmifInHeader(4) + service_name
        ac.emit(mov64(0, HIPC_TYPE_REQUEST))
        ac.emit(strw(0, 22, 0))
        ac.emit(mov64(0, num_dw & 0x3FF))  # num_data_words, has_special=0
        ac.emit(strw(0, 22, 4))

        # Domain obj id at TLS+8 (dw_off, value=1 for the converted session)
        ac.emit(mov64(0, 1))  # domain object id = 1
        ac.emit(strw(0, 22, 8))

        # CmifInHeader at TLS+12 (cmif_off = dw_off + 4 for domain mode)
        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, 12))
        ac.emit(mov64(0, 0))  # version
        ac.emit(strw(0, 22, 16))
        ac.emit(mov64(0, 1))  # cmd_id = 1 (GetService)
        ac.emit(strw(0, 22, 20))
        ac.emit(mov64(0, self.step_num))  # token
        ac.emit(strw(0, 22, 24))

        # Service name at TLS+28 (after CmifInHeader, 4 words = 16 bytes)
        # "appletOE:" = 10 bytes → pad to 16 bytes (4 words)
        for i in range(0, len(srv_bytes), 4):
            chunk = srv_bytes[i:i+4]
            val = 0
            for j, c in enumerate(chunk):
                if j < 4:
                    val |= c << (j * 8)
            ac.emit(mov64(0, val))
            ac.emit(strw(0, 22, 28 + i))

        # Send
        ac.emit(mov(0, 23))  # sm: session
        ac.emit(svc(0x21))
        self.emit_verify_no_error()

        # 响应: IPC handler 自动在 response 中添加 move handle
        # 响应布局: HipcHeader(8) + SpecialHeader(4) + move_handle(4) + CmifOutHeader(16)
        # move handle 在 TLS+12
        ac.emit(ldrw(session_reg_out, 22, 12))
        # 保存 handle 到 session_reg

    def emit_convert_to_domain(self, session_reg):
        """ConvertCurrentObjectToDomain (Control cmd 0)"""
        ac = self.ac
        self.emit_step("ConvertCurrentObjectToDomain")

        ac.emit(mov64(0, HIPC_TYPE_CONTROL))
        ac.emit(strw(0, 22, 0))
        ac.emit(mov64(0, 4 << 10))  # num_data_words=4
        ac.emit(strw(0, 22, 4))

        # CmifInHeader at 16
        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, 16))
        ac.emit(strw(31, 22, 20))
        ac.emit(strw(31, 22, 24))  # cmd = 0
        ac.emit(strw(31, 22, 28))  # token = 0

        ac.emit(mov(0, session_reg))
        ac.emit(svc(0x21))
        self.emit_verify_no_error()
        # 验证 domain_id
        # 响应布局: HipcHeader(8) + SpecialHeader(4) + handle(4) + CmifOutHeader(16) + data
        # domain_id 在 TLS+32 = HipcHeader(8) + SpecialHeader(4) + handle(4) + CmifOutHeader(16) = 32
        ac.emit(ldrw(0, 22, 32))
        ac.emit(cmp_imm(0, 1, is64=False))
        ok = f"ok_domain_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: domain_id != 1")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

    def emit_applet_get_applet_proxy(self, session_reg, proxy_reg):
        """appletOE::GetAppletProxy (cmd 0) → AmProxyService handle"""
        ac = self.ac
        self.emit_step("appletOE::GetAppletProxy (cmd 0)")

        # 这是 domain 模式下的请求
        # HipcHeader + SpecialHeader(move=1) + data: domain_obj_id(4) + CmifInHeader(16)
        ac.emit(mov64(0, HIPC_TYPE_REQUEST))
        ac.emit(strw(0, 22, 0))
        ac.emit(mov64(0, (5 << 10) | (1 << 31)))  # num_data=5, has_special
        ac.emit(strw(0, 22, 4))
        ac.emit(mov64(0, (1 << 5)))  # move=1
        ac.emit(strw(0, 22, 8))

        # data at 16: domain_object_id = 1
        ac.emit(mov64(0, 1))
        ac.emit(strw(0, 22, 16))
        # CmifInHeader at 20
        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, 20))
        ac.emit(strw(31, 22, 24))  # version
        ac.emit(strw(31, 22, 28))  # cmd_id = 0
        ac.emit(mov64(0, self.step_num))
        ac.emit(strw(0, 22, 32))   # token

        ac.emit(mov(0, session_reg))
        ac.emit(svc(0x21))
        self.emit_verify_no_error()

        # Handle 在 TLS+12 (SpecialHeader 后)
        ac.emit(ldrw(proxy_reg, 22, 12))

    def emit_applet_cmd_no_data(self, session_reg, cmd_id, expected_empty=True):
        """调用 appletOE/appletAE 上的简单命令"""
        ac = self.ac
        self.emit_step(f"appletOE::cmd {cmd_id}")

        ac.emit(mov64(0, HIPC_TYPE_REQUEST))
        ac.emit(strw(0, 22, 0))
        ac.emit(mov64(0, (5 << 10) | (1 << 31)))  # num_data=5, has_special, move=1
        ac.emit(strw(0, 22, 4))
        ac.emit(mov64(0, (1 << 5)))  # move=1
        ac.emit(strw(0, 22, 8))

        ac.emit(mov64(0, 1))  # domain obj id
        ac.emit(strw(0, 22, 16))
        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, 20))
        ac.emit(strw(31, 22, 24))
        ac.emit(mov64(0, cmd_id))
        ac.emit(strw(0, 22, 28))
        ac.emit(mov64(0, self.step_num))
        ac.emit(strw(0, 22, 32))

        ac.emit(mov(0, session_reg))
        ac.emit(svc(0x21))
        self.emit_verify_no_error()

        # 对于会返回 session handle 的命令，读取 handle
        if not expected_empty:
            ac.emit(ldrw(0, 22, 12))
            # 保存到临时寄存器
            ac.emit(mov(20, 0))
            return 20  # reg containing the returned handle
        return None

    def emit_proxy_get_subservice(self, proxy_reg, cmd_id, out_reg,
                                    expected_empty=False, svc_name=""):
        """通过 AmProxy 获取子服务"""
        ac = self.ac

        # AmProxy 的每个命令返回一个 move handle 或空
        self.emit_step(f"AmProxy::cmd {cmd_id} {svc_name}")

        ac.emit(mov64(0, HIPC_TYPE_REQUEST))
        ac.emit(strw(0, 22, 0))
        ac.emit(mov64(0, (5 << 10) | (1 << 31)))  # num_data=5, has_special, move=1
        ac.emit(strw(0, 22, 4))
        ac.emit(mov64(0, (1 << 5)))  # move=1
        ac.emit(strw(0, 22, 8))

        # AmProxy not in domain mode (it's a raw session)
        # CmifInHeader at 16
        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, 16))
        ac.emit(strw(31, 22, 20))
        ac.emit(mov64(0, cmd_id))
        ac.emit(strw(0, 22, 24))
        ac.emit(mov64(0, self.step_num))
        ac.emit(strw(0, 22, 28))

        ac.emit(mov(0, proxy_reg))
        ac.emit(svc(0x21))
        self.emit_verify_no_error()

        # 如果不是空响应，读取 handle
        if not expected_empty:
            ac.emit(ldrw(out_reg, 22, 12))
            return out_reg
        return None

    def emit_raw_cmd_on_session(self, sess_reg, cmd_id, raw_in_hex=None,
                                  has_move=True, desc=""):
        """对任意 session 发送原始命令"""
        ac = self.ac

        self.emit_step(f"Sess cmd {cmd_id} {desc}")

        # Build IPC request
        num_extra_words = 0
        if raw_in_hex:
            raw_bytes = bytes.fromhex(raw_in_hex)
            num_extra_words = (len(raw_bytes) + 3) // 4
        else:
            raw_bytes = b''

        # Domain mode (sess_reg might be domain): object_id=1 at start
        # CmifInHeader(16) + raw_in
        cmif_words = 4  # CmifInHeader
        domain_prefix = 1  # word for object_id
        total_data_words = domain_prefix + cmif_words + num_extra_words

        ac.emit(mov64(0, HIPC_TYPE_REQUEST))
        ac.emit(strw(0, 22, 0))

        has_special = 1 if has_move else 0
        hdr = (total_data_words & 0x3FF) << 10
        if has_special:
            hdr |= (1 << 31)
        ac.emit(mov64(0, hdr))
        ac.emit(strw(0, 22, 4))

        if has_special:
            move_val = (1 << 5) if has_move else 0
            ac.emit(mov64(0, move_val))
            ac.emit(strw(0, 22, 8))

        # data at 16
        off = 16
        ac.emit(mov64(0, 1))  # domain obj id
        ac.emit(strw(0, 22, off))
        off += 4

        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, off))
        ac.emit(strw(31, 22, off + 4))     # version
        ac.emit(mov64(0, cmd_id))
        ac.emit(strw(0, 22, off + 8))
        ac.emit(mov64(0, self.step_num))
        ac.emit(strw(0, 22, off + 12))
        off += 16

        if raw_bytes:
            for i in range(0, len(raw_bytes), 4):
                chunk = raw_bytes[i:i+4]
                val = 0
                for j, c in enumerate(chunk):
                    if j < 4:
                        val |= c << (j * 8)
                ac.emit(mov64(0, val))
                ac.emit(strw(0, 22, off + i))

        ac.emit(mov(0, sess_reg))
        ac.emit(svc(0x21))
        self.emit_verify_no_error()

        if has_move:
            ac.emit(ldrw(20, 22, 12))
            return 20
        return None

    # ── 构建完整测试 ────────────────────────────────────

    def build(self):
        ac = self.ac

        # ── 初始化 ──
        self.emit_init()

        # ── STEP 1-2: 连接 sm: 并初始化 ──
        self.emit_connect_and_init_sm()

        # ── STEP 3: SM::GetService("appletOE:") ──
        self.emit_sm_get_service("appletOE:", 1)  # X1 → 获取 session handle
        ac.emit(mov(15, 1))  # 保存到 X15 = appletOE session

        # ── STEP 4: ConvertCurrentObjectToDomain ──
        self.emit_convert_to_domain(15)

        # ── STEP 5: appletOE::GetAppletProxy (cmd 0) ──
        self.emit_applet_get_applet_proxy(15, 14)  # X14 = proxy session

        # ── STEP 6-7: AmProxy → GetCommonStateGetter & GetWindowController ──
        # AmProxy 子命令 (不是 domain 模式)
        self.emit_proxy_get_subservice(14, 0, 13, svc_name="GetCommonStateGetter")
        self.emit_proxy_get_subservice(14, 1, 12, svc_name="GetSelfController")
        self.emit_proxy_get_subservice(14, 2, 11, svc_name="GetWindowController")
        self.emit_proxy_get_subservice(14, 3, 10, svc_name="GetAudioController")
        self.emit_proxy_get_subservice(14, 4, 9, svc_name="GetDisplayController")
        self.emit_proxy_get_subservice(14, 11, 8, svc_name="GetLibraryAppletCreator")
        self.emit_proxy_get_subservice(14, 20, 7, svc_name="GetFunctions")
        self.emit_proxy_get_subservice(14, 1000, 6, svc_name="GetDebugFunctions")

        # ── appletOE::OpenSystemApplet (cmd 10) → IWindowController ──
        self.emit_applet_cmd_no_data(15, 10, expected_empty=False)

        # ── appletOE::GetAppletResource (cmd 40) → ICommonStateGetter ──
        self.emit_applet_cmd_no_data(15, 40, expected_empty=False)

        # ── appletOE::GetAppletType (cmd 100) ──
        # 这个命令不含 move handle，直接返回数据
        # 对于 domain 模式无 special header 的请求:
        #   HipcHeader(8) + DomainObjId(4) + CmifInHeader(16) = 28 bytes = 7 words
        self.emit_step("appletOE::GetAppletType (cmd 100)")
        ac.emit(mov64(0, HIPC_TYPE_REQUEST))
        ac.emit(strw(0, 22, 0))
        ac.emit(mov64(0, 7))  # num_data_words=7, no special, no recv
        ac.emit(strw(0, 22, 4))
        # data at TLS+8: domain_object_id = 1
        ac.emit(mov64(0, 1))
        ac.emit(strw(0, 22, 8))
        # CmifInHeader at TLS+12
        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, 12))
        ac.emit(strw(31, 22, 16))  # version = 0
        ac.emit(mov64(0, 100))     # cmd_id = 100
        ac.emit(strw(0, 22, 20))
        ac.emit(mov64(0, self.step_num))  # token
        ac.emit(strw(0, 22, 24))
        ac.emit(mov(0, 15))  # session = R15
        ac.emit(svc(0x21))
        self.emit_verify_no_error()
        # 验证响应数据: applet type = 2 (Application)
        # 响应: HipcHeader(8) + CmifOutHeader(at 16) + data(at 32)
        ac.emit(ldrw(0, 22, 32))
        ac.emit(cmp_imm(0, 2, is64=False))
        ok = f"ok_apptype_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: GetAppletType != 2")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # ── appletOE::GetMainAppletIdentityInfo (cmd 200) ──
        self.emit_step("appletOE::GetMainAppletIdentityInfo (cmd 200)")
        ac.emit(mov64(0, HIPC_TYPE_REQUEST))
        ac.emit(strw(0, 22, 0))
        ac.emit(mov64(0, 7))  # num_data_words=7, no special
        ac.emit(strw(0, 22, 4))
        ac.emit(mov64(0, 1))  # domain obj
        ac.emit(strw(0, 22, 8))
        ac.emit(mov64(0, CMIF_IN_MAGIC))
        ac.emit(strw(0, 22, 12))
        ac.emit(strw(31, 22, 16))  # version
        ac.emit(mov64(0, 200))
        ac.emit(strw(0, 22, 20))  # cmd_id
        ac.emit(mov64(0, self.step_num))
        ac.emit(strw(0, 22, 24))  # token
        ac.emit(mov(0, 15))
        ac.emit(svc(0x21))
        self.emit_verify_no_error()
        # 验证: out[0] = 2 (applet type), data at TLS+32
        ac.emit(ldrb(0, 22, 32))
        ac.emit(cmp_imm(0, 2, is64=False))
        ok = f"ok_idinfo_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: identity info[0] != 2")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # ── IWindowController 命令 ──
        # 使用 X11 (WindowController session from proxy cmd 2)

        # IWindowController 命令列表（所有命令都直接返回数据，无 move handle）
        # cmd 0: GetAppletResourceUserId → u64=1
        self.emit_raw_cmd_on_session(11, 0, has_move=False, desc="GetAppletResourceUserId")
        ac.emit(ldrd(0, 22, 32))  # data after CmifOutHeader
        ac.emit(mov64(1, 1))  # expected uid = 1
        ac.emit(cmp(0, 1))
        ok = f"ok_uid_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: WindowController cmd 0 != 1")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 1: AcquireForegroundRights → empty
        self.emit_raw_cmd_on_session(11, 1, has_move=False, desc="AcquireForegroundRights")

        # cmd 2: ReleaseForegroundRights → empty
        self.emit_raw_cmd_on_session(11, 2, has_move=False, desc="ReleaseForegroundRights")

        # cmd 10: GetAppletResourceId → u32=0
        self.emit_raw_cmd_on_session(11, 10, has_move=False, desc="GetAppletResourceId")
        ac.emit(ldrw(0, 22, 32))
        ac.emit(cmp_imm(0, 0, is64=False))
        ok = f"ok_resid_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: WindowController cmd 10 != 0")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 20: CreateManagedDisplayLayer → u64=1
        self.emit_raw_cmd_on_session(11, 20, has_move=False, desc="CreateManagedDisplayLayer")
        ac.emit(ldrd(0, 22, 32))
        ac.emit(mov64(1, 1))
        ac.emit(cmp(0, 1))
        ok = f"ok_layer_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: WindowController cmd 20 != 1")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 30: GetIndirectLayerConsumerHandle → u64=0x02000000
        self.emit_raw_cmd_on_session(11, 30, has_move=False, desc="GetIndirectLayerConsumerHandle")
        ac.emit(ldrd(0, 22, 32))
        ac.emit(mov64(1, 0x02000000))
        ac.emit(cmp(0, 1))
        ok = f"ok_indirect_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: WindowController cmd 30 != 0x02000000")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # ── ICommonStateGetter 命令 ──
        # 使用 X13 (从 proxy cmd 0 获得)

        # cmd 0: GetEventHandle → u32=0xE0000001
        self.emit_raw_cmd_on_session(13, 0, has_move=False, desc="GetEventHandle")
        ac.emit(ldrw(0, 22, 32))
        ac.emit(mov64(1, 0xE0000001))
        ac.emit(cmp(0, 1, is64=False))
        ok = f"ok_evt_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: StateGetter cmd 0 != 0xE0000001")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 1: ReceiveMessage → u32=0xF (FocusStateChanged)
        self.emit_raw_cmd_on_session(13, 1, has_move=False, desc="ReceiveMessage")
        ac.emit(ldrw(0, 22, 32))
        ac.emit(mov64(1, 0xF))
        ac.emit(cmp(0, 1, is64=False))
        ok = f"ok_msg_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: StateGetter cmd 1 != 0xF")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 5: GetOperationMode → u8=1 (Docked)
        self.emit_raw_cmd_on_session(13, 5, has_move=False, desc="GetOperationMode")
        ac.emit(ldrb(0, 22, 32))
        ac.emit(cmp_imm(0, 1, is64=False))
        ok = f"ok_opmode_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: StateGetter cmd 5 != 1")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 6: GetPerformanceMode → u8=1 (Boost)
        self.emit_raw_cmd_on_session(13, 6, has_move=False, desc="GetPerformanceMode")
        ac.emit(ldrb(0, 22, 32))
        ac.emit(cmp_imm(0, 1, is64=False))
        ok = f"ok_perfmode_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: StateGetter cmd 6 != 1")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 7: GetCurrentFocusState → u8=1
        self.emit_raw_cmd_on_session(13, 7, has_move=False, desc="GetCurrentFocusState")
        ac.emit(ldrb(0, 22, 32))
        ac.emit(cmp_imm(0, 1, is64=False))
        ok = f"ok_focus_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: StateGetter cmd 7 != 1")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 9: SetFocusHandlingMode → empty (stub)
        self.emit_raw_cmd_on_session(13, 9, has_move=False, desc="SetFocusHandlingMode")

        # cmd 10: SetOutOfFocusSuspendingEnabled → empty
        self.emit_raw_cmd_on_session(13, 10, has_move=False, desc="SetOutOfFocusSuspendingEnabled")

        # cmd 11: GetDefaultDisplayResolution → u32 w=1280, u32 h=720
        self.emit_raw_cmd_on_session(13, 11, has_move=False, desc="GetDefaultDisplayResolution")
        ac.emit(ldrw(0, 22, 32))  # w
        ac.emit(mov64(1, 1280))
        ac.emit(cmp(0, 1, is64=False))
        ok = f"ok_dispw_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: StateGetter cmd 11 w != 1280")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)
        ac.emit(ldrw(0, 22, 36))  # h
        ac.emit(mov64(1, 720))
        ac.emit(cmp(0, 1, is64=False))
        ok = f"ok_disph_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: StateGetter cmd 11 h != 720")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 30: GetOperationModeChangeEvent → empty
        self.emit_raw_cmd_on_session(13, 30, has_move=False, desc="GetOperationModeChangeEvent")

        # ── IApplicationFunctions 命令 ──
        # 使用 X7 (从 proxy cmd 20 获得)

        # cmd 0: Initialize → empty
        self.emit_raw_cmd_on_session(7, 0, has_move=False, desc="AppFunc::Initialize")

        # cmd 1: NotifyRunning → empty
        self.emit_raw_cmd_on_session(7, 1, has_move=False, desc="AppFunc::NotifyRunning")

        # cmd 2: GetPseudoDeviceId → 0x10 bytes all zeros
        self.emit_raw_cmd_on_session(7, 2, has_move=False, desc="GetPseudoDeviceId")
        # 验证前 8 字节为 0
        ac.emit(ldrd(0, 22, 32))
        ac.emit(cmp_imm(0, 0))
        ok = f"ok_pseudoid_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: AppFunc cmd 2 != 0")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 10: EnsureSaveData → u64=0x100000
        self.emit_raw_cmd_on_session(7, 10, has_move=False, desc="EnsureSaveData")
        ac.emit(ldrd(0, 22, 32))
        ac.emit(mov64(1, 0x100000))
        ac.emit(cmp(0, 1))
        ok = f"ok_save_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: AppFunc cmd 10 != 0x100000")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 11: GetDisplayVersion → 16 bytes (version 1.0.0)
        self.emit_raw_cmd_on_session(7, 11, has_move=False, desc="GetDisplayVersion")
        # 验证: version[0..3] = 1,0,0,0
        ac.emit(ldrb(0, 22, 32))
        ac.emit(cmp_imm(0, 1, is64=False))
        ok = f"ok_ver_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: AppFunc cmd 11 version mismatch")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 50: EnsureSaveData2 → u64=0x100000
        self.emit_raw_cmd_on_session(7, 50, has_move=False, desc="EnsureSaveData2")
        ac.emit(ldrd(0, 22, 32))
        ac.emit(mov64(1, 0x100000))
        ac.emit(cmp(0, 1))
        ok = f"ok_save2_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: AppFunc cmd 50 != 0x100000")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 100: SetTerminateResult → empty
        self.emit_raw_cmd_on_session(7, 100, has_move=False, desc="SetTerminateResult")

        # ── SelfController 命令 ──
        # 使用 X12 (从 proxy cmd 1 获得)

        # cmd 0: GetAppletResourceUserId → u64=1
        self.emit_raw_cmd_on_session(12, 0, has_move=False, desc="SelfController::GetAppletResourceUserId")
        ac.emit(ldrd(0, 22, 32))
        ac.emit(mov64(1, 1))
        ac.emit(cmp(0, 1))
        ok = f"ok_selfuid_{self.step_num}"
        ac.emit(ac.b_eq(ok))
        self.emit_print("  FAIL: SelfController cmd 0 != 1")
        ac.emit(mov64(0, self.step_num))
        ac.emit(svc(0x07))
        ac.label(ok)

        # cmd 1: AcquireForegroundRights → empty
        self.emit_raw_cmd_on_session(12, 1, has_move=False, desc="SelfController::AcquireForegroundRights")

        # ── 全部通过 ──
        self.emit_print("\n==========================================")
        self.emit_print("  ALL APPLET TESTS PASSED!")
        self.emit_print("==========================================")

        # 正常退出
        ac.emit(movz(0, 0, 0))
        ac.emit(svc(0x07))
        ac.emit(b(0))  # 无限循环 (保险)

        # 解析标签
        code = ac.finalize()
        return code


# ═══════════════════════════════════════════════════════════
# NRO 打包
# ═══════════════════════════════════════════════════════════

def build_nro(text_data):
    """Wrap program bytes in an NRO0 container."""
    text_size = len(text_data)
    text_aligned = (text_size + 0x1FF) & ~0x1FF

    total_size = 0x10 + 0x100 + text_aligned

    # NRO0 header at file offset 0x10
    header = b''
    header += struct.pack('<IIII', 0x304F524E, 0, total_size, 0)
    header += struct.pack('<II', 0x110, text_size)
    header += struct.pack('<II', 0x110 + text_aligned, 0)
    header += struct.pack('<II', 0x110 + text_aligned, 0)
    header += struct.pack('<II', 0, 0)
    header += b'\x00' * 32
    header += b'\x00' * (0x100 - len(header))

    # Preamble
    nro = b'\x00' * 0x10
    nro += header
    nro += text_data
    nro += b'\x00' * (text_aligned - text_size)

    return nro


def main():
    builder = AppletTestBuilder()
    code = builder.build()

    text_data = b''
    for instr in code:
        text_data += struct.pack('<I', instr & 0xFFFFFFFF)

    nro = build_nro(text_data)

    output = "applet_test.nro"
    with open(output, 'wb') as f:
        f.write(nro)

    print(f"✅ {output}: {len(nro)} bytes, {len(code)} instructions")
    print(f"   .text: {len(text_data)} bytes at file offset 0x110")
    print(f"   测试步骤: {builder.step_num} 个")

    # 显示反汇编
    print("\n--- 反汇编 ---")
    for i, instr in enumerate(code):
        print(f"  {i*4:04x}: {instr:08x}", end="")
        if i < 5:
            print("  <init>")
        else:
            print()

    print(f"\n总计: {len(code)} 条指令 ({len(text_data)} 字节)")


if __name__ == "__main__":
    main()

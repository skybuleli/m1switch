// ── Maxwell Decoder Tests ──────────────────────────────────
// Tests decoding known 64-bit Maxwell shader instructions
// into the M1Switch intermediate representation (ShaderIr).
//
// Corrected Maxwell instruction encoding (field positions):
//   bits[12:5]  = opcode (8 bits, range 0x00-0xFF)
//   bits[19:13] = src0 / dest register
//   bits[26:20] = src1 / coord register
//   bits[33:27] = src2 (3-input only)
//   bits[35:33] = predicate source
//   bits[42:40] = predicate dest (ISETP/FSETP)
//   bits[44:39] = dest register
//   bits[45]    = neg_src0
//   bits[46]    = neg_src1
//   bits[47]    = abs_src0
//   bits[48]    = abs_src1 (3-input only)
//   bits[49]    = saturate
//   bits[54:51] = MUFU sub-op
//
// Memory format:
//   bits[19:13] = dest/src GPR
//   bits[34:27] = offset
//   bits[44:39] = address GPR
//
// Branch format:
//   bits[26:7]  = branch target offset (20-bit signed, in 8-byte units)
//   bits[35:33] = predicate source
//
// S2R format:
//   bits[19:13] = dest GPR
//   bits[44:39] = special register index
//
// Texture format:
//   bits[19:13] = dest GPR
//   bits[33:27] = coordinate GPR
//   bits[44:39] = texture index

#include "gpu/shader/MaxwellDecoder.h"

// ═════════════════════════════════════════════════════════════
// ALU instruction decode tests
// ═════════════════════════════════════════════════════════════

// ── Test 1: FADD r2, r1, r3 (2-input ALU ── float add) ───
TEST(MaxwellDecoder_FADD) {
    // opcode=0x01 (FADD), src0=r1, src1=r3, dest=r2
    u64 raw = (0x01ULL << 5);  // bits[12:5]  = opcode
    raw |= (1ULL << 13);       // bits[19:13] = src0=r1
    raw |= (3ULL << 20);       // bits[26:20] = src1=r3
    raw |= (2ULL << 39);       // bits[44:39] = dest=r2

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::Alu2In, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::FADD, (u32)di.opcode);
    CHECK_EQ(2, di.field.src_count);
    CHECK_EQ(1, di.field.gpr_src0);
    CHECK_EQ(3, di.field.gpr_src1);
    CHECK_EQ(2, di.field.gpr_dest);
    CHECK_EQ(false, di.field.neg_src0);
    CHECK_EQ(false, di.field.neg_src1);
    CHECK_EQ(false, di.field.saturate);

    // Convert to IR and verify operands
    ShaderInstruction ir = di.ToIr();
    CHECK_EQ((u32)ShaderOpcode::FADD, (u32)ir.opcode);
    CHECK_EQ(2, ir.src_count);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
    CHECK_EQ(1, ir.src[0].gpr.reg_index);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[1].type);
    CHECK_EQ(3, ir.src[1].gpr.reg_index);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
    CHECK_EQ(2, ir.dest.gpr.reg_index);
    return true;
}

// ── Test 2: IADD3 r5, r1, r2, r3 (3-input ALU) ─────────
TEST(MaxwellDecoder_IADD3) {
    // opcode=0x10 (IADD3), src0=r1, src1=r2, src2=r3, dest=r5
    u64 raw = (0x10ULL << 5);  // bits[12:5]  = opcode
    raw |= (1ULL << 13);       // bits[19:13] = src0=r1
    raw |= (2ULL << 20);       // bits[26:20] = src1=r2
    raw |= (3ULL << 27);       // bits[33:27] = src2=r3
    raw |= (5ULL << 39);       // bits[44:39] = dest=r5

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::Alu3In, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::IADD3, (u32)di.opcode);
    CHECK_EQ(3, di.field.src_count);
    CHECK_EQ(1, di.field.gpr_src0);
    CHECK_EQ(2, di.field.gpr_src1);
    CHECK_EQ(3, di.field.gpr_src2);
    CHECK_EQ(5, di.field.gpr_dest);

    ShaderInstruction ir = di.ToIr();
    CHECK_EQ(3, ir.src_count);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
    CHECK_EQ(1, ir.src[0].gpr.reg_index);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[1].type);
    CHECK_EQ(2, ir.src[1].gpr.reg_index);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[2].type);
    CHECK_EQ(3, ir.src[2].gpr.reg_index);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
    CHECK_EQ(5, ir.dest.gpr.reg_index);
    return true;
}

// ── Test 3: MUFU.SIN r3, r1 (1-input ALU with sub-op) ───
TEST(MaxwellDecoder_MUFU) {
    // opcode=0x06 (MUFU), src0=r1, dest=r3, sub-op=SIN (1)
    u64 raw = (0x06ULL << 5);  // bits[12:5]  = opcode=MUFU
    raw |= (1ULL << 13);       // bits[19:13] = src0=r1
    raw |= (3ULL << 39);       // bits[44:39] = dest=r3
    raw |= (1ULL << 51);       // bits[54:51] = MUFU sub-op = SIN

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::Alu1In, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::MUFU, (u32)di.opcode);
    CHECK_EQ(1, di.field.src_count);
    CHECK_EQ(1, di.field.gpr_src0);
    CHECK_EQ(3, di.field.gpr_dest);
    CHECK_EQ((u32)MufuOp::SIN, (u32)di.field.mufu_op);

    ShaderInstruction ir = di.ToIr();
    CHECK_EQ(1, ir.src_count);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
    CHECK_EQ(1, ir.src[0].gpr.reg_index);
    CHECK_EQ((u32)MufuOp::SIN, (u32)ir.src[0].mufu_op);
    return true;
}

// ── Test 4: FADD with modifiers (negate, abs, saturate) ──
TEST(MaxwellDecoder_FADD_Modifiers) {
    // opcode=0x01 (FADD), src0=r2, src1=r4, dest=r6
    // neg_src0=1, abs_src0=1, saturate=1
    u64 raw = (0x01ULL << 5);
    raw |= (2ULL << 13);       // src0=r2
    raw |= (4ULL << 20);       // src1=r4
    raw |= (6ULL << 39);       // dest=r6
    raw |= (1ULL << 45);       // neg_src0
    raw |= (1ULL << 47);       // abs_src0
    raw |= (1ULL << 49);       // saturate

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ(true, di.field.neg_src0);
    CHECK_EQ(false, di.field.neg_src1);
    CHECK_EQ(true, di.field.abs_src0);
    CHECK_EQ(true, di.field.saturate);

    ShaderInstruction ir = di.ToIr();
    CHECK_EQ(true, ir.src[0].negate);
    CHECK_EQ(true, ir.src[0].absolute);
    CHECK_EQ(true, ir.dest.saturate);
    return true;
}

// ── Test 5: ISETP.NE p1, r2, r3 (integer compare) ────────
TEST(MaxwellDecoder_ISETP) {
    // opcode=0x17 (ISETP), src0=r2, src1=r3, pred_src=1, pred_dest=1
    u64 raw = (0x17ULL << 5);
    raw |= (2ULL << 13);       // src0=r2  (bits[19:13])
    raw |= (3ULL << 20);       // src1=r3  (bits[26:20])
    raw |= (1ULL << 33);       // pred_src=1 (bits[35:33])
    raw |= (1ULL << 40);       // pred_dest=1 (bits[42:40])

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::ISetp, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::ISETP, (u32)di.opcode);
    CHECK_EQ(2, di.field.src_count);
    CHECK_EQ(2, di.field.gpr_src0);
    CHECK_EQ(3, di.field.gpr_src1);
    CHECK_EQ(1, di.field.pred_src);
    CHECK_EQ(1, di.field.pred_dest);
    CHECK_EQ(true, di.field.sets_cc);
    return true;
}

// ── Test 6: FSETP (float compare) ─────────────────────────
TEST(MaxwellDecoder_FSETP) {
    // opcode=0x30 (FSETP), src0=r1, src1=r2, pred_dest=2
    u64 raw = (0x30ULL << 5);
    raw |= (1ULL << 13);       // src0=r1
    raw |= (2ULL << 20);       // src1=r2
    raw |= (2ULL << 40);       // pred_dest=2 (bits[42:40])

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::FSetp, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::FSETP, (u32)di.opcode);
    CHECK_EQ(2, di.field.src_count);
    CHECK_EQ(1, di.field.gpr_src0);
    CHECK_EQ(2, di.field.gpr_src1);
    CHECK_EQ(2, di.field.pred_dest);
    CHECK_EQ(true, di.field.is_float);
    return true;
}

// ── Test 7: IMAD r6, r1, r2, r3 (integer multiply-add) ───
TEST(MaxwellDecoder_IMAD) {
    // opcode=0x11 (IMAD), src0=r1, src1=r2, src2=r3, dest=r6
    u64 raw = (0x11ULL << 5);
    raw |= (1ULL << 13);       // src0=r1
    raw |= (2ULL << 20);       // src1=r2
    raw |= (3ULL << 27);       // src2=r3
    raw |= (6ULL << 39);       // dest=r6

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::Alu3In, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::IMAD, (u32)di.opcode);
    CHECK_EQ(3, di.field.src_count);
    CHECK_EQ(1, di.field.gpr_src0);
    CHECK_EQ(2, di.field.gpr_src1);
    CHECK_EQ(3, di.field.gpr_src2);
    CHECK_EQ(6, di.field.gpr_dest);
    return true;
}

// ═════════════════════════════════════════════════════════════
// Memory load/store decode tests
// ═════════════════════════════════════════════════════════════

// ── Test 8: LD (load) from global memory ──────────────────
TEST(MaxwellDecoder_LD_Global) {
    // opcode=0x40 (LD), dest=r5, address=r2, offset=0x10, mem_space=Global
    u64 raw0 = (0x40ULL << 5);   // bits[12:5] = LD opcode
    raw0 |= (5ULL << 13);         // bits[19:13] = dest=r5
    raw0 |= (0x10ULL << 27);      // bits[34:27] = offset=0x10
    raw0 |= (2ULL << 39);         // bits[44:39] = address r2

    // Raw1 encodes memory space in bits[47:45]
    u64 raw1 = (0ULL << 45);      // bits[47:45] = Global (0)

    u8 data[16];
    memcpy(data,      &raw0, 8);
    memcpy(data + 8,  &raw1, 8);

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne(data, sizeof(data), 0);

    CHECK_EQ((u32)MaxwellFormat::MemoryLoad, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::LD, (u32)di.opcode);
    CHECK_EQ(1, di.field.src_count);
    CHECK_EQ(5, di.field.gpr_dest);
    CHECK_EQ(2, di.field.gpr_src0);
    CHECK_EQ(0x10, di.field.mem_offset);
    CHECK_EQ((u32)MemorySpace::Global, (u32)di.field.mem_space);

    ShaderInstruction ir = di.ToIr();
    CHECK_EQ((u32)ShaderOpcode::LD, (u32)ir.opcode);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
    CHECK_EQ(5, ir.dest.gpr.reg_index);
    CHECK_EQ((u32)MemorySpace::Global, (u32)ir.mem_space);
    return true;
}

// ── Test 9: ST (store) to global memory ──────────────────
TEST(MaxwellDecoder_ST_Global) {
    // opcode=0x50 (ST), src=r3, address=r1, offset=0x08
    u64 raw0 = (0x50ULL << 5);   // bits[12:5] = ST opcode
    raw0 |= (3ULL << 13);         // bits[19:13] = src=r3
    raw0 |= (0x08ULL << 27);      // bits[34:27] = offset=0x08
    raw0 |= (1ULL << 39);         // bits[44:39] = address r1

    u64 raw1 = (0ULL << 45);      // bits[47:45] = Global (0)

    u8 data[16];
    memcpy(data,      &raw0, 8);
    memcpy(data + 8,  &raw1, 8);

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne(data, sizeof(data), 0);

    CHECK_EQ((u32)MaxwellFormat::MemoryStore, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::ST, (u32)di.opcode);
    CHECK_EQ(1, di.field.src_count);
    CHECK_EQ(3, di.field.gpr_dest);
    CHECK_EQ(1, di.field.gpr_src0);
    CHECK_EQ(0x08, di.field.mem_offset);
    CHECK_EQ((u32)MemorySpace::Global, (u32)di.field.mem_space);

    ShaderInstruction ir = di.ToIr();
    CHECK_EQ((u32)ShaderOpcode::ST, (u32)ir.opcode);
    CHECK_EQ((u32)MemorySpace::Global, (u32)ir.mem_space);
    return true;
}

// ── Test 10: LDS (load shared) with offset ───────────────
TEST(MaxwellDecoder_LDS) {
    // opcode=0x43 (LDS), dest=r4, address=r1, offset=0x20, mem_space=Shared
    u64 raw0 = (0x43ULL << 5);   // bits[12:5] = LDS opcode
    raw0 |= (4ULL << 13);         // bits[19:13] = dest=r4
    raw0 |= (0x20ULL << 27);      // bits[34:27] = offset=0x20
    raw0 |= (1ULL << 39);         // bits[44:39] = address r1

    u64 raw1 = (1ULL << 45);      // bits[47:45] = Shared (1)

    u8 data[16];
    memcpy(data,      &raw0, 8);
    memcpy(data + 8,  &raw1, 8);

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne(data, sizeof(data), 0);

    CHECK_EQ((u32)MaxwellFormat::MemoryLoad, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::LDS, (u32)di.opcode);
    CHECK_EQ(4, di.field.gpr_dest);
    CHECK_EQ(0x20, di.field.mem_offset);
    CHECK_EQ((u32)MemorySpace::Shared, (u32)di.field.mem_space);
    return true;
}

// ── Test 11: STG (store global) ─────────────────────────
TEST(MaxwellDecoder_STG) {
    // opcode=0x51 (STG), src=r7, address=r2, offset=0x00
    u64 raw0 = (0x51ULL << 5);   // bits[12:5] = STG opcode
    raw0 |= (7ULL << 13);         // bits[19:13] = src=r7
    raw0 |= (0x00ULL << 27);      // bits[34:27] = offset=0
    raw0 |= (2ULL << 39);         // bits[44:39] = address r2

    u64 raw1 = (0ULL << 45);      // bits[47:45] = Global (0)

    u8 data[16];
    memcpy(data,      &raw0, 8);
    memcpy(data + 8,  &raw1, 8);

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne(data, sizeof(data), 0);

    CHECK_EQ((u32)MaxwellFormat::MemoryStore, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::STG, (u32)di.opcode);
    CHECK_EQ(7, di.field.gpr_dest);
    CHECK_EQ(2, di.field.gpr_src0);
    return true;
}

// ═════════════════════════════════════════════════════════════
// Control flow decode tests
// ═════════════════════════════════════════════════════════════

// ── Test 12: BRA (branch) ────────────────────────────────
TEST(MaxwellDecoder_BRA) {
    // opcode=0x81 (BRA), offset=+3 (target=pc+8+3*8=0x20 at pc=0)
    u64 raw = (0x81ULL << 5);     // bits[12:5] = BRA opcode
    raw |= (3ULL << 13);          // bits[26:13] = branch offset = 3 (14-bit signed)

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::Branch, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::BRA, (u32)di.opcode);
    CHECK_EQ(0, di.field.src_count);
    // target = pc + 8 + offset * 8 = 0 + 8 + 3*8 = 32 = 0x20
    CHECK_EQ(0x20, di.field.branch_target);

    ShaderInstruction ir = di.ToIr();
    CHECK_EQ((u32)ShaderOpcode::BRA, (u32)ir.opcode);
    CHECK_EQ(1, ir.src_count);
    CHECK_EQ((u32)OperandType::Label, (u32)ir.src[0].type);
    // Label is branch_target / 8 = 0x20 / 8 = 4
    CHECK_EQ(4, ir.src[0].label);
    return true;
}

// ── Test 13: EXIT instruction ────────────────────────────
TEST(MaxwellDecoder_EXIT) {
    // opcode=0x80 (EXIT)
    u64 raw = (0x80ULL << 5);     // bits[12:5] = EXIT opcode

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::Exit, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::EXIT, (u32)di.opcode);
    CHECK_EQ(0, di.field.src_count);
    return true;
}

// ── Test 14: CALL subroutine ─────────────────────────────
TEST(MaxwellDecoder_CALL) {
    // opcode=0x82 (CALL), offset=-2 (target=pc+8-2*8=pc-8)
    u64 raw = (0x82ULL << 5);     // bits[12:5] = CALL opcode
    // Negative offset: -2 = 0x3FFE in 14-bit signed, at bits[26:13]
    raw |= (0x3FFEULL << 13);     // bits[26:13] = signed offset -2

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::Branch, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::CALL, (u32)di.opcode);
    // target = 0 + 8 + (-2)*8 = -8 = 0xFFFFFFF8
    CHECK_EQ(0xFFFFFFF8, di.field.branch_target);
    return true;
}

// ═════════════════════════════════════════════════════════════
// Special register & texture decode tests
// ═════════════════════════════════════════════════════════════

// ── Test 15: S2R (special register read) ─────────────────
TEST(MaxwellDecoder_S2R) {
    // opcode=0x70 (S2R), dest=r5, special register=LaneId(0)
    u64 raw = (0x70ULL << 5);     // bits[12:5] = S2R opcode
    raw |= (5ULL << 13);          // bits[19:13] = dest=r5
    raw |= (0ULL << 39);          // bits[44:39] = special reg index = LaneId

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::S2R, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::S2R, (u32)di.opcode);
    CHECK_EQ(5, di.field.gpr_dest);
    CHECK_EQ((u32)SpecialReg::LaneId, (u32)di.field.gpr_src0);

    ShaderInstruction ir = di.ToIr();
    CHECK_EQ((u32)ShaderOpcode::S2R, (u32)ir.opcode);
    CHECK_EQ(1, ir.src_count);
    CHECK_EQ((u32)OperandType::SpecialReg, (u32)ir.src[0].type);
    CHECK_EQ((u32)SpecialReg::LaneId, (u32)ir.src[0].special_reg);
    return true;
}

// ── Test 16: TEX (texture sample) ────────────────────────
TEST(MaxwellDecoder_TEX) {
    // opcode=0x60 (TEX), dest=r5, coord=r2, tex_index=3, sampler=1
    u64 raw0 = (0x60ULL << 5);   // bits[12:5] = TEX opcode
    raw0 |= (5ULL << 13);         // bits[19:13] = dest=r5
    raw0 |= (2ULL << 27);         // bits[33:27] = coord=r2
    raw0 |= (3ULL << 39);         // bits[44:39] = tex_index=3

    u64 raw1 = (1ULL << 44);      // bits[49:44] = sampler_idx=1

    u8 data[16];
    memcpy(data,      &raw0, 8);
    memcpy(data + 8,  &raw1, 8);

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne(data, sizeof(data), 0);

    CHECK_EQ((u32)MaxwellFormat::Texture, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::TEX, (u32)di.opcode);
    CHECK_EQ(5, di.field.gpr_dest);
    CHECK_EQ(2, di.field.gpr_src0);
    CHECK_EQ(3, di.field.tex_index);
    CHECK_EQ(1, di.field.sampler_idx);

    ShaderInstruction ir = di.ToIr();
    CHECK_EQ((u32)ShaderOpcode::TEX, (u32)ir.opcode);
    CHECK_EQ(2, ir.src_count);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
    CHECK_EQ(2, ir.src[0].gpr.reg_index);
    CHECK_EQ((u32)OperandType::Texture, (u32)ir.src[1].type);
    CHECK_EQ(3, ir.src[1].texture.tex_index);
    CHECK_EQ(1, ir.src[1].texture.sampler_index);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
    CHECK_EQ(5, ir.dest.gpr.reg_index);
    return true;
}

// ═════════════════════════════════════════════════════════════
// Mixed program & opcode table tests
// ═════════════════════════════════════════════════════════════

// ── Test 17: Full program decode ─────────────────────────
// Decodes a short program: FADD + IADD3 + MUFU + ISETP
TEST(MaxwellDecoder_FullProgram) {
    // Instruction 1: FADD r2, r1, r3  (at offset 0)
    u64 inst1 = (0x01ULL << 5) | (1ULL << 13) | (3ULL << 20) | (2ULL << 39);
    // Instruction 2: IADD3 r5, r1, r2, r3  (at offset 8)
    u64 inst2 = (0x10ULL << 5) | (1ULL << 13) | (2ULL << 20) | (3ULL << 27) | (5ULL << 39);
    // Instruction 3: MUFU.SIN r3, r1  (at offset 16)
    u64 inst3 = (0x06ULL << 5) | (1ULL << 13) | (3ULL << 39) | (1ULL << 51);
    // Instruction 4: ISETP p1, r2, r3  (at offset 24)
    u64 inst4 = (0x17ULL << 5) | (2ULL << 13) | (3ULL << 20) | (1ULL << 33) | (1ULL << 40);

    u8 data[32];
    memcpy(data,      &inst1, 8);
    memcpy(data + 8,  &inst2, 8);
    memcpy(data + 16, &inst3, 8);
    memcpy(data + 24, &inst4, 8);

    MaxwellDecoder decoder;
    ShaderProgram program = decoder.Decode(data, sizeof(data),
                                            ShaderStage::VertexA);

    CHECK(!decoder.HasErrors());
    CHECK_EQ(4, program.instructions.size());
    CHECK_EQ(ShaderStage::VertexA, program.stage);

    // Verify each decoded instruction
    CHECK_EQ((u32)ShaderOpcode::FADD, (u32)program.instructions[0].opcode);
    CHECK_EQ(2, program.instructions[0].src_count);
    CHECK_EQ(0, program.instructions[0].pc);

    CHECK_EQ((u32)ShaderOpcode::IADD3, (u32)program.instructions[1].opcode);
    CHECK_EQ(3, program.instructions[1].src_count);
    CHECK_EQ(8, program.instructions[1].pc);

    CHECK_EQ((u32)ShaderOpcode::MUFU, (u32)program.instructions[2].opcode);
    CHECK_EQ(1, program.instructions[2].src_count);
    CHECK_EQ(16, program.instructions[2].pc);

    CHECK_EQ((u32)ShaderOpcode::ISETP, (u32)program.instructions[3].opcode);
    CHECK_EQ(2, program.instructions[3].src_count);
    CHECK_EQ(24, program.instructions[3].pc);

    // Metadata
    CHECK(program.hash != 0);
    CHECK_EQ(32, program.program_size);
    CHECK(program.num_gprs_used >= 4);  // r1,r2,r3,r5 used → 4 unique GPRs
    CHECK_EQ(2, program.num_preds_used); // pred_used[0] from zero-predicate tracking + pred_used[1] from ISETP
    return true;
}

// ── Test 18: Known opcode decode verification ──────────
TEST(MaxwellDecoder_OpcodeTable) {
    struct TestVector {
        u64 raw;
        ShaderOpcode expected_op;
        MaxwellFormat expected_fmt;
    };

    TestVector tests[] = {
        // ALU float: opcode at bits[12:5], src0 at bits[19:13],
        // src1 at bits[26:20], dest at bits[44:39]
        { (0x01ULL << 5) | (1ULL << 13) | (1ULL << 20) | (1ULL << 39),
          ShaderOpcode::FADD, MaxwellFormat::Alu2In },
        { (0x02ULL << 5) | (1ULL << 13) | (1ULL << 20) | (1ULL << 39),
          ShaderOpcode::FMUL, MaxwellFormat::Alu2In },
        { (0x03ULL << 5) | (1ULL << 13) | (1ULL << 20) | (1ULL << 39),
          ShaderOpcode::FMAX, MaxwellFormat::Alu2In },

        // ALU unary (Alu1In): opcode at bits[12:5], src0 at bits[19:13], dest at bits[44:39]
        { (0x07ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::FRCP, MaxwellFormat::Alu1In },
        { (0x08ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::FRSQ, MaxwellFormat::Alu1In },
        { (0x09ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::FSQRT, MaxwellFormat::Alu1In },

        // ALU integer (Alu2In)
        { (0x12ULL << 5) | (1ULL << 13) | (1ULL << 20) | (1ULL << 39),
          ShaderOpcode::IADD, MaxwellFormat::Alu2In },
        { (0x13ULL << 5) | (1ULL << 13) | (1ULL << 20) | (1ULL << 39),
          ShaderOpcode::ISUB, MaxwellFormat::Alu2In },
        { (0x14ULL << 5) | (1ULL << 13) | (1ULL << 20) | (1ULL << 39),
          ShaderOpcode::IMUL, MaxwellFormat::Alu2In },

        // Integer shift (Alu1In)
        { (0x1EULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::SHL, MaxwellFormat::Alu1In },
        { (0x1FULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::SHR, MaxwellFormat::Alu1In },

        // Compare
        { (0x30ULL << 5) | (1ULL << 13) | (1ULL << 20) | (1ULL << 40),
          ShaderOpcode::FSETP, MaxwellFormat::FSetp },

        // 3-integer opcodes (Alu3In)
        { (0x10ULL << 5) | (1ULL << 13) | (2ULL << 20) | (3ULL << 27) | (4ULL << 39),
          ShaderOpcode::IADD3, MaxwellFormat::Alu3In },
        { (0x11ULL << 5) | (1ULL << 13) | (2ULL << 20) | (3ULL << 27) | (4ULL << 39),
          ShaderOpcode::IMAD, MaxwellFormat::Alu3In },

        // Memory load/store (through MemoryFormat, needs 16 bytes)
        // These use the full 16-byte decode path
        { (0x40ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::LD, MaxwellFormat::MemoryLoad },
        { (0x41ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::LG, MaxwellFormat::MemoryLoad },
        { (0x43ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::LDS, MaxwellFormat::MemoryLoad },
        { (0x44ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::LDC, MaxwellFormat::MemoryLoad },
        { (0x50ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::ST, MaxwellFormat::MemoryStore },
        { (0x51ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::STG, MaxwellFormat::MemoryStore },
        { (0x53ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::STS, MaxwellFormat::MemoryStore },

        // Texture
        { (0x60ULL << 5) | (1ULL << 13) | (1ULL << 27),
          ShaderOpcode::TEX, MaxwellFormat::Texture },
        { (0x64ULL << 5) | (1ULL << 13) | (1ULL << 27),
          ShaderOpcode::TXQ, MaxwellFormat::Texture },

        // Special
        { (0x70ULL << 5) | (1ULL << 13) | (1ULL << 39),
          ShaderOpcode::S2R, MaxwellFormat::S2R },

        // Control flow
        { (0x80ULL << 5),  ShaderOpcode::EXIT, MaxwellFormat::Exit },
        { (0x81ULL << 5) | (1ULL << 13), ShaderOpcode::BRA, MaxwellFormat::Branch },
        { (0x83ULL << 5) | (1ULL << 13), ShaderOpcode::SSY, MaxwellFormat::Sync },
        { (0x85ULL << 5),  ShaderOpcode::RET, MaxwellFormat::Exit },
        { (0x86ULL << 5),  ShaderOpcode::KIL, MaxwellFormat::Exit },
    };

    MaxwellDecoder decoder;
    for (const auto& t : tests) {
        // For memory/texture instructions, pass 16 bytes
        u32 data_size = 8;
        u8 data[16] = {};
        memcpy(data, &t.raw, 8);

        // Memory and texture formats read raw1
        switch (t.expected_fmt) {
        case MaxwellFormat::MemoryLoad:
        case MaxwellFormat::MemoryStore:
        case MaxwellFormat::Texture:
            data_size = 16;
            break;
        default:
            break;
        }

        DecodedInst di = decoder.DecodeOne(data, data_size, 0);
        if ((u32)t.expected_fmt != (u32)di.fmt) {
            LOG_ERROR("OpcodeTable test: expected fmt=%d, got fmt=%d for op=%d raw=0x%llx",
                      (int)t.expected_fmt, (int)di.fmt, (int)t.expected_op, t.raw);
        }
        CHECK_EQ((u32)t.expected_fmt, (u32)di.fmt);
        CHECK_EQ((u32)t.expected_op, (u32)di.opcode);
    }
    return true;
}

// ═════════════════════════════════════════════════════════════
// Edge case tests
// ═════════════════════════════════════════════════════════════

// ── Test 19: gpr_src0=0 behavior in ToIr ─────────────────
// GPR 0 (r0) is a valid Maxwell register and is preserved
// in the IR. When gpr_src0==0, it now correctly populates
// ir.src[0] as OperandType::GPR with reg_index=0.
TEST(MaxwellDecoder_ZeroGprSource) {
    // FADD with src0=r0: the raw instruction has src0=0
    u64 raw = (0x01ULL << 5);    // opcode=FADD
    raw |= (0ULL << 13);          // bits[19:13] = src0=r0 (zero)
    raw |= (2ULL << 20);          // bits[26:20] = src1=r2
    raw |= (1ULL << 39);          // bits[44:39] = dest=r1

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ(2, di.field.src_count);
    CHECK_EQ(0, di.field.gpr_src0);
    CHECK_EQ(2, di.field.gpr_src1);
    CHECK_EQ(1, di.field.gpr_dest);

    // ToIr: src_count is preserved, and r0 is a valid GPR.
    ShaderInstruction ir = di.ToIr();
    CHECK_EQ(2, ir.src_count);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
    CHECK_EQ(0, ir.src[0].gpr.reg_index);
    CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[1].type);
    CHECK_EQ(2, ir.src[1].gpr.reg_index);
    return true;
}

// ── Test 20: Truncated instruction (error handling) ───────
TEST(MaxwellDecoder_Truncated) {
    // Only 4 bytes of data, instruction needs 8
    u8 data[4] = {0x01, 0x00, 0x00, 0x00};

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne(data, sizeof(data), 0);

    CHECK_EQ((u32)MaxwellFormat::Unknown, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::NOP, (u32)di.opcode);
    CHECK(decoder.HasErrors());
    return true;
}

// ── Test 21: Decode empty program ─────────────────────────
TEST(MaxwellDecoder_EmptyProgram) {
    MaxwellDecoder decoder;
    ShaderProgram program = decoder.Decode(nullptr, 0,
                                            ShaderStage::Fragment);

    CHECK_EQ(0, program.instructions.size());
    CHECK_EQ(ShaderStage::Fragment, program.stage);
    return true;
}

// ── Test 22: R2P (register to predicate) ─────────────────
TEST(MaxwellDecoder_R2P) {
    // opcode=0x71 (R2P) - PredicateReg format
    u64 raw = (0x71ULL << 5);

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::PredicateReg, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::R2P, (u32)di.opcode);
    return true;
}

// ── Test 23: BREAK opcode ────────────────────────────────
TEST(MaxwellDecoder_BREAK) {
    // opcode=0x88 (BREAK) - Exit format
    u64 raw = (0x88ULL << 5);

    MaxwellDecoder decoder;
    DecodedInst di = decoder.DecodeOne((const u8*)&raw, sizeof(raw), 0);

    CHECK_EQ((u32)MaxwellFormat::Exit, (u32)di.fmt);
    CHECK_EQ((u32)ShaderOpcode::BREAK, (u32)di.opcode);
    return true;
}

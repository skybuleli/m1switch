// ── Shader Decode Integration Test ──────────────────────────
// Decodes a realistic synthesized Maxwell shader program that
// exercises all instruction format types: S2R, Alu3In, Alu2In,
// Alu1In, ISetp, FSetp, MemoryLoad, MemoryStore, Texture, Exit.
//
// The program resembles a simplified vertex shader:
//   1-2. Read special registers (S2R LaneId, BlockIdX)
//   3.   Integer add (IADD3)
//   4-6. Float ops (FADD, FMUL, IADD)
//   7-8. Compare & set predicates (ISETP, FSETP)
//   9.   Trig (MUFU.SIN)
//   10.  Reciprocal (FRCP)
//   11.  Global load (LG)
//   12.  Shared load (LDS)
//   13.  Texture sample (TEX)
//   14.  Global store (STG)
//   15.  Exit (EXIT)

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "common/Log.h"
#include "common/Types.h"
#include "gpu/shader/MaxwellDecoder.h"

// ── Encode helpers ──────────────────────────────────────────
// All Maxwell field positions use the corrected 8-bit opcode layout:
//   bits[12:5]  = opcode
//   bits[19:13] = src0 / dest
//   bits[26:20] = src1
//   bits[33:27] = src2
//   bits[35:33] = pred_src
//   bits[42:40] = pred_dest
//   bits[44:39] = dest / tex_index / addr
//   bits[45]    = neg_src0
//   bits[46]    = neg_src1
//   bits[47]    = abs_src0
//   bits[48]    = abs_src1
//   bits[49]    = saturate
//   bits[54:51] = MUFU sub-op
//   bits[34:27] = mem_offset
//
// Memory raw1: bits[47:45] = mem_space
// Texture raw1: bits[49:44] = sampler_idx

static u64 EncodeAlu2In(u32 opcode, u32 src0, u32 src1, u32 dest) {
    return ((u64)opcode << 5) | ((u64)src0 << 13) | ((u64)src1 << 20) | ((u64)dest << 39);
}

static u64 EncodeAlu3In(u32 opcode, u32 src0, u32 src1, u32 src2, u32 dest) {
    return ((u64)opcode << 5) | ((u64)src0 << 13) | ((u64)src1 << 20) | ((u64)src2 << 27) | ((u64)dest << 39);
}

static u64 EncodeAlu1In(u32 opcode, u32 src0, u32 dest) {
    return ((u64)opcode << 5) | ((u64)src0 << 13) | ((u64)dest << 39);
}

static u64 EncodeS2R(u32 dest, u32 special_reg) {
    return (0x70ULL << 5) | ((u64)dest << 13) | ((u64)special_reg << 39);
}

static u64 EncodeISETP(u32 src0, u32 src1, u32 pred_src, u32 pred_dest) {
    return (0x17ULL << 5) | ((u64)src0 << 13) | ((u64)src1 << 20)
         | ((u64)pred_src << 33) | ((u64)pred_dest << 40);
}

static u64 EncodeFSETP(u32 src0, u32 src1, u32 pred_dest) {
    return (0x30ULL << 5) | ((u64)src0 << 13) | ((u64)src1 << 20) | ((u64)pred_dest << 40);
}

static u64 EncodeMUFU(u32 src0, u32 dest, u32 sub_op) {
    return (0x06ULL << 5) | ((u64)src0 << 13) | ((u64)dest << 39) | ((u64)sub_op << 51);
}

static void EncodeMemory(u8* data, u32 offset, u32 opcode, u32 reg, u32 addr_reg, u32 mem_offs, u32 mem_space) {
    u64 raw0 = ((u64)opcode << 5) | ((u64)reg << 13) | ((u64)mem_offs << 27) | ((u64)addr_reg << 39);
    u64 raw1 = ((u64)mem_space << 45);
    std::memcpy(data + offset,      &raw0, 8);
    std::memcpy(data + offset + 8,  &raw1, 8);
}

static void EncodeTexture(u8* data, u32 offset, u32 dest, u32 coord, u32 tex_idx, u32 sampler) {
    u64 raw0 = (0x60ULL << 5) | ((u64)dest << 13) | ((u64)coord << 27) | ((u64)tex_idx << 39);
    u64 raw1 = ((u64)sampler << 44);
    std::memcpy(data + offset,      &raw0, 8);
    std::memcpy(data + offset + 8,  &raw1, 8);
}

// ═════════════════════════════════════════════════════════════
// Test: Decode a complete synthesized Maxwell shader
// ═════════════════════════════════════════════════════════════

TEST(Maxwell_FullShaderDecode) {
    // Build a 152-byte Maxwell shader program
    // Layout:  15 instructions, 10×8 bytes + 4×16 bytes = 144 bytes + EXIT (8)
    //           Memory/texture instructions read raw1, so they occupy 16 bytes each.
    const u32 TOTAL_SIZE = 152;
    u8 data[TOTAL_SIZE];
    std::memset(data, 0, TOTAL_SIZE);

    // Instruction 1 (offset 0x00): S2R r1, LaneId
    {
        u64 raw = EncodeS2R(1, 0);
        std::memcpy(data + 0x00, &raw, 8);
    }

    // Instruction 2 (offset 0x08): S2R r2, BlockIdX
    {
        u64 raw = EncodeS2R(2, 7);
        std::memcpy(data + 0x08, &raw, 8);
    }

    // Instruction 3 (offset 0x10): IADD3 r3, r1, r2, r0
    {
        u64 raw = EncodeAlu3In(0x10, 1, 2, 0, 3);
        std::memcpy(data + 0x10, &raw, 8);
    }

    // Instruction 4 (offset 0x18): FADD r4, r3, r1
    {
        u64 raw = EncodeAlu2In(0x01, 3, 1, 4);
        std::memcpy(data + 0x18, &raw, 8);
    }

    // Instruction 5 (offset 0x20): FMUL r5, r4, r0
    {
        u64 raw = EncodeAlu2In(0x02, 4, 0, 5);
        std::memcpy(data + 0x20, &raw, 8);
    }

    // Instruction 6 (offset 0x28): IADD r6, r5, r2
    {
        u64 raw = EncodeAlu2In(0x12, 5, 2, 6);
        std::memcpy(data + 0x28, &raw, 8);
    }

    // Instruction 7 (offset 0x30): ISETP.NE p1, r6, r0
    {
        u64 raw = EncodeISETP(6, 0, 1, 1);
        std::memcpy(data + 0x30, &raw, 8);
    }

    // Instruction 8 (offset 0x38): FSETP.LT p2, r6, r0
    {
        u64 raw = EncodeFSETP(6, 0, 2);
        std::memcpy(data + 0x38, &raw, 8);
    }

    // Instruction 9 (offset 0x40): MUFU.SIN r7, r6
    {
        u64 raw = EncodeMUFU(6, 7, (u32)MufuOp::SIN);
        std::memcpy(data + 0x40, &raw, 8);
    }

    // Instruction 10 (offset 0x48): FRCP r8, r7
    {
        u64 raw = EncodeAlu1In(0x07, 7, 8);
        std::memcpy(data + 0x48, &raw, 8);
    }

    // Instruction 11 (offset 0x50): LG r9, [r3 + 0x04] (Global load)
    EncodeMemory(data, 0x50, 0x41, 9, 3, 4, 0);  // mem_space=0=Global

    // Instruction 12 (offset 0x60): LDS r10, [r1 + 0x10] (Shared load)
    EncodeMemory(data, 0x60, 0x43, 10, 1, 0x10, 1);  // mem_space=1=Shared

    // Instruction 13 (offset 0x70): TEX r11, r9, tex=0, sampler=0
    EncodeTexture(data, 0x70, 11, 9, 0, 0);

    // Instruction 14 (offset 0x80): STG r11, [r10 + 0x00] (Global store)
    EncodeMemory(data, 0x80, 0x51, 11, 10, 0, 0);  // mem_space=0=Global

    // Instruction 15 (offset 0x90): EXIT
    {
        u64 raw = (0x80ULL << 5);
        std::memcpy(data + 0x90, &raw, 8);
    }

    // ── Now decode the full program ───────────────────────
    MaxwellDecoder decoder;
    ShaderProgram program = decoder.Decode(data, TOTAL_SIZE, ShaderStage::VertexA);

    CHECK(!decoder.HasErrors());

    // ── Verify program metadata ──────────────────────────
    CHECK_EQ(15, program.instructions.size());
    CHECK_EQ(ShaderStage::VertexA, program.stage);
    CHECK_EQ(TOTAL_SIZE, program.program_size);
    CHECK(program.hash != 0);

    // GPRs used: r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11 = 12 unique
    CHECK(program.num_gprs_used >= 12);

    // Predicates used: p0 (from zero-tracking), p1 (ISETP), p2 (FSETP) = 3
    CHECK_EQ(3, program.num_preds_used);

    // Uses varyings (LD/ST) and textures
    CHECK(program.uses_varyings);   // LG + STG
    CHECK(program.uses_textures);   // TEX

    // ── Verify each decoded instruction ───────────────────
    // Instruction 0: S2R r1, LaneId @ pc=0
    {
        const auto& ir = program.instructions[0];
        CHECK_EQ((u32)ShaderOpcode::S2R, (u32)ir.opcode);
        CHECK_EQ(0, ir.pc);
        CHECK_EQ(1, ir.src_count);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
        CHECK_EQ(1, ir.dest.gpr.reg_index);
        CHECK_EQ((u32)OperandType::SpecialReg, (u32)ir.src[0].type);
        CHECK_EQ((u32)SpecialReg::LaneId, (u32)ir.src[0].special_reg);
    }

    // Instruction 1: S2R r2, BlockIdX @ pc=8
    {
        const auto& ir = program.instructions[1];
        CHECK_EQ((u32)ShaderOpcode::S2R, (u32)ir.opcode);
        CHECK_EQ(8, ir.pc);
        CHECK_EQ(1, ir.src_count);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
        CHECK_EQ(2, ir.dest.gpr.reg_index);
        CHECK_EQ((u32)OperandType::SpecialReg, (u32)ir.src[0].type);
        CHECK_EQ((u32)SpecialReg::BlockIdX, (u32)ir.src[0].special_reg);
    }

    // Instruction 2: IADD3 r3, r1, r2, r0 @ pc=16
    {
        const auto& ir = program.instructions[2];
        CHECK_EQ((u32)ShaderOpcode::IADD3, (u32)ir.opcode);
        CHECK_EQ(16, ir.pc);
        CHECK_EQ(3, ir.src_count);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
        CHECK_EQ(1, ir.src[0].gpr.reg_index);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[1].type);
        CHECK_EQ(2, ir.src[1].gpr.reg_index);
        // src[2] is r0 — GPR 0 is a valid register
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[2].type);
        CHECK_EQ(0, ir.src[2].gpr.reg_index);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
        CHECK_EQ(3, ir.dest.gpr.reg_index);
    }

    // Instruction 3: FADD r4, r3, r1 @ pc=24
    {
        const auto& ir = program.instructions[3];
        CHECK_EQ((u32)ShaderOpcode::FADD, (u32)ir.opcode);
        CHECK_EQ(24, ir.pc);
        CHECK_EQ(2, ir.src_count);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
        CHECK_EQ(3, ir.src[0].gpr.reg_index);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[1].type);
        CHECK_EQ(1, ir.src[1].gpr.reg_index);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
        CHECK_EQ(4, ir.dest.gpr.reg_index);
    }

    // Instruction 4: FMUL r5, r4, r0 @ pc=32
    {
        const auto& ir = program.instructions[4];
        CHECK_EQ((u32)ShaderOpcode::FMUL, (u32)ir.opcode);
        CHECK_EQ(32, ir.pc);
        CHECK_EQ(2, ir.src_count);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
        CHECK_EQ(4, ir.src[0].gpr.reg_index);
        // src[1] is r0 — GPR 0 is a valid register
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[1].type);
        CHECK_EQ(0, ir.src[1].gpr.reg_index);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
        CHECK_EQ(5, ir.dest.gpr.reg_index);
    }

    // Instruction 5: IADD r6, r5, r2 @ pc=40
    {
        const auto& ir = program.instructions[5];
        CHECK_EQ((u32)ShaderOpcode::IADD, (u32)ir.opcode);
        CHECK_EQ(40, ir.pc);
        CHECK_EQ(2, ir.src_count);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
        CHECK_EQ(5, ir.src[0].gpr.reg_index);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[1].type);
        CHECK_EQ(2, ir.src[1].gpr.reg_index);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
        CHECK_EQ(6, ir.dest.gpr.reg_index);
    }

    // Instruction 6: ISETP.NE p1, r6, r0 @ pc=48
    {
        const auto& ir = program.instructions[6];
        CHECK_EQ((u32)ShaderOpcode::ISETP, (u32)ir.opcode);
        CHECK_EQ(48, ir.pc);
        CHECK_EQ(2, ir.src_count);
        CHECK_EQ(true, ir.pred_guard);
        CHECK_EQ(1, ir.pred_guard_index);
    }

    // Instruction 7: FSETP.LT p2, r6, r0 @ pc=56
    {
        const auto& ir = program.instructions[7];
        CHECK_EQ((u32)ShaderOpcode::FSETP, (u32)ir.opcode);
        CHECK_EQ(56, ir.pc);
        CHECK_EQ(2, ir.src_count);
        // No guard predicate (pred_src=0), only destination predicate set
        CHECK_EQ(false, ir.pred_guard);
        CHECK_EQ(0, ir.pred_guard_index);
    }

    // Instruction 8: MUFU.SIN r7, r6 @ pc=64
    {
        const auto& ir = program.instructions[8];
        CHECK_EQ((u32)ShaderOpcode::MUFU, (u32)ir.opcode);
        CHECK_EQ(64, ir.pc);
        CHECK_EQ(1, ir.src_count);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
        CHECK_EQ(6, ir.src[0].gpr.reg_index);
        CHECK_EQ((u32)MufuOp::SIN, (u32)ir.src[0].mufu_op);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
        CHECK_EQ(7, ir.dest.gpr.reg_index);
    }

    // Instruction 9: FRCP r8, r7 @ pc=72
    {
        const auto& ir = program.instructions[9];
        CHECK_EQ((u32)ShaderOpcode::FRCP, (u32)ir.opcode);
        CHECK_EQ(72, ir.pc);
        CHECK_EQ(1, ir.src_count);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
        CHECK_EQ(7, ir.src[0].gpr.reg_index);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
        CHECK_EQ(8, ir.dest.gpr.reg_index);
    }

    // Instruction 10: LG r9, [r3 + 0x04] @ pc=80
    {
        const auto& ir = program.instructions[10];
        CHECK_EQ((u32)ShaderOpcode::LG, (u32)ir.opcode);
        CHECK_EQ(80, ir.pc);
        CHECK_EQ(1, ir.src_count);
        CHECK_EQ((u32)MemorySpace::Global, (u32)ir.mem_space);
    }

    // Instruction 11: LDS r10, [r1 + 0x10] @ pc=96
    {
        const auto& ir = program.instructions[11];
        CHECK_EQ((u32)ShaderOpcode::LDS, (u32)ir.opcode);
        CHECK_EQ(96, ir.pc);
        CHECK_EQ(1, ir.src_count);
        CHECK_EQ((u32)MemorySpace::Shared, (u32)ir.mem_space);
    }

    // Instruction 12: TEX r11, r9, tex=0, sampler=0 @ pc=112
    {
        const auto& ir = program.instructions[12];
        CHECK_EQ((u32)ShaderOpcode::TEX, (u32)ir.opcode);
        CHECK_EQ(112, ir.pc);
        CHECK_EQ(2, ir.src_count);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.src[0].type);
        CHECK_EQ(9, ir.src[0].gpr.reg_index);
        CHECK_EQ((u32)OperandType::Texture, (u32)ir.src[1].type);
        CHECK_EQ(0, ir.src[1].texture.tex_index);
        CHECK_EQ(0, ir.src[1].texture.sampler_index);
        CHECK_EQ((u32)OperandType::GPR, (u32)ir.dest.type);
        CHECK_EQ(11, ir.dest.gpr.reg_index);
    }

    // Instruction 13: STG r11, [r10 + 0x00] @ pc=128
    {
        const auto& ir = program.instructions[13];
        CHECK_EQ((u32)ShaderOpcode::STG, (u32)ir.opcode);
        CHECK_EQ(128, ir.pc);
        CHECK_EQ(1, ir.src_count);
        CHECK_EQ((u32)MemorySpace::Global, (u32)ir.mem_space);
    }

    // Instruction 14: EXIT @ pc=144
    {
        const auto& ir = program.instructions[14];
        CHECK_EQ((u32)ShaderOpcode::EXIT, (u32)ir.opcode);
        CHECK_EQ(144, ir.pc);
        CHECK_EQ(0, ir.src_count);
    }

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Decode as different shader stages
// ═════════════════════════════════════════════════════════════

TEST(Maxwell_ShaderStageDecode) {
    // Use a minimal program: just S2R + EXIT
    u8 data[16];
    std::memset(data, 0, sizeof(data));

    // S2R r1, LaneId
    u64 raw = EncodeS2R(1, 0);
    std::memcpy(data, &raw, 8);
    // EXIT
    raw = (0x80ULL << 5);
    std::memcpy(data + 8, &raw, 8);

    // Decode as different stages and verify metadata
    MaxwellDecoder decoder;

    ShaderProgram vs = decoder.Decode(data, sizeof(data), ShaderStage::VertexA);
    CHECK(!decoder.HasErrors());
    CHECK_EQ(ShaderStage::VertexA, vs.stage);
    CHECK_EQ(2, vs.instructions.size());

    ShaderProgram fs = decoder.Decode(data, sizeof(data), ShaderStage::Fragment);
    CHECK(!decoder.HasErrors());
    CHECK_EQ(ShaderStage::Fragment, fs.stage);
    CHECK_EQ(2, fs.instructions.size());

    ShaderProgram cs = decoder.Decode(data, sizeof(data), ShaderStage::Compute);
    CHECK(!decoder.HasErrors());
    CHECK_EQ(ShaderStage::Compute, cs.stage);
    CHECK_EQ(2, cs.instructions.size());

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Shader binary with varying instruction sizes (8/16 bytes)
// ═════════════════════════════════════════════════════════════

TEST(Maxwell_MixedSizes) {
    // Program: S2R (8) → LG (16) → FADD (8) → STG (16) → EXIT (8)
    // Total: 8 + 16 + 8 + 16 + 8 = 56 bytes
    const u32 TOTAL = 56;
    u8 data[TOTAL];
    std::memset(data, 0, TOTAL);

    // S2R r1, LaneId @ 0x00
    {
        u64 raw = EncodeS2R(1, 0);
        std::memcpy(data + 0x00, &raw, 8);
    }
    // LG r2, [r1 + 0x08] @ 0x08 (16 bytes)
    EncodeMemory(data, 0x08, 0x41, 2, 1, 8, 0);
    // FADD r3, r2, r0 @ 0x18
    {
        u64 raw = EncodeAlu2In(0x01, 2, 0, 3);
        std::memcpy(data + 0x18, &raw, 8);
    }
    // STG r3, [r2 + 0x00] @ 0x20 (16 bytes)
    EncodeMemory(data, 0x20, 0x51, 3, 2, 0, 0);
    // EXIT @ 0x30
    {
        u64 raw = (0x80ULL << 5);
        std::memcpy(data + 0x30, &raw, 8);
    }

    MaxwellDecoder decoder;
    ShaderProgram program = decoder.Decode(data, TOTAL, ShaderStage::Fragment);

    CHECK(!decoder.HasErrors());
    CHECK_EQ(5, program.instructions.size());

    // Verify PCS
    CHECK_EQ(0,   program.instructions[0].pc);   // S2R
    CHECK_EQ(8,   program.instructions[1].pc);   // LG (starts at 8, takes 16 bytes)
    CHECK_EQ(24,  program.instructions[2].pc);   // FADD (starts at 24)
    CHECK_EQ(32,  program.instructions[3].pc);   // STG (starts at 32, takes 16 bytes)
    CHECK_EQ(48,  program.instructions[4].pc);   // EXIT (starts at 48)

    // Verify correct decoding through 16-byte instructions
    CHECK_EQ((u32)ShaderOpcode::S2R,  (u32)program.instructions[0].opcode);
    CHECK_EQ((u32)ShaderOpcode::LG,   (u32)program.instructions[1].opcode);
    CHECK_EQ((u32)ShaderOpcode::FADD, (u32)program.instructions[2].opcode);
    CHECK_EQ((u32)ShaderOpcode::STG,  (u32)program.instructions[3].opcode);
    CHECK_EQ((u32)ShaderOpcode::EXIT, (u32)program.instructions[4].opcode);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Shader binary hash consistency
// ═════════════════════════════════════════════════════════════

TEST(Maxwell_ShaderHashConsistency) {
    // Same shader should produce the same hash
    u8 data[24];
    std::memset(data, 0, sizeof(data));

    // S2R r5, InvocationId
    u64 raw = (0x70ULL << 5) | (5ULL << 13) | (13ULL << 39);
    std::memcpy(data, &raw, 8);
    // EXIT
    raw = (0x80ULL << 5);
    std::memcpy(data + 8, &raw, 8);

    MaxwellDecoder decoder1, decoder2;
    ShaderProgram p1 = decoder1.Decode(data, sizeof(data), ShaderStage::Fragment);
    ShaderProgram p2 = decoder2.Decode(data, sizeof(data), ShaderStage::Fragment);

    CHECK(!decoder1.HasErrors());
    CHECK(!decoder2.HasErrors());

    // Same binary → same hash
    CHECK_EQ(p1.hash, p2.hash);

    // Different binary → different hash
    data[0] ^= 0xFF;  // corrupt first byte
    ShaderProgram p3 = decoder1.Decode(data, sizeof(data), ShaderStage::Fragment);
    CHECK(p3.hash != p2.hash);

    return true;
}

#include "gpu/shader/MaxwellDecoder.h"
#include "common/Log.h"

#include <cstring>
#include <cstdarg>

// ═══════════════════════════════════════════════════════════
// Maxwell ISA Decoder Implementation
// ═══════════════════════════════════════════════════════════

// ── Helper to extract bit fields ─────────────────────────────
static u32 Bits(u64 v, int hi, int lo) {
    int bits = hi - lo + 1;
    return (u32)((v >> lo) & ((1ULL << bits) - 1));
}

static bool Bit(u64 v, int b) {
    return (v >> b) & 1;
}

static s32 SignExtend(u32 val, int bits) {
    s32 s = (s32)val;
    if (s & (1 << (bits - 1)))
        s |= ~((1 << bits) - 1);
    return s;
}

// ── Error logging ───────────────────────────────────────────
void MaxwellDecoder::LogError(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    errors_.push_back(buf);
    LOG_ERROR("MaxwellDecoder: %s", buf);
}

// ═══════════════════════════════════════════════════════════
// Format detection
// ═══════════════════════════════════════════════════════════

MaxwellFormat MaxwellDecoder::DetectFormat(u32 opcode_id, u64 raw) const {
    // Opcode IDs are roughly grouped by category:
    //   0x00-0x0F: ALU float (FMAD=0x00, FADD=0x01, etc.)
    //   0x10-0x1F: ALU integer
    //   0x20-0x2F: Type conversion
    //   0x30-0x3F: Compare
    //   0x40-0x4F: Memory load
    //   0x50-0x5F: Memory store
    //   0x60-0x6F: Texture
    //   0x70-0x7F: Special
    //   0x80-0x8F: Control flow

    switch (opcode_id) {
    // ── ALU float ─────────────────────────────────
    case 0x00: // FMAD (3-input: dest = src0 * src1 + src2)
        return MaxwellFormat::Alu3In;

    case 0x01: // FADD
    case 0x02: // FMUL
    case 0x03: // FMAX
    case 0x04: // FMIN
    case 0x05: // FSAT
        return MaxwellFormat::Alu2In;

    case 0x06: // MUFU
    case 0x07: // FRCP
    case 0x08: // FRSQ
    case 0x09: // FSQRT
    case 0x0A: // FEX2
    case 0x0B: // FLG2
    case 0x0C: // FSIN
    case 0x0D: // FCOS
        return MaxwellFormat::Alu1In;

    // ── ALU integer ───────────────────────────────
    case 0x10: // IADD3
    case 0x11: // IMAD
        return MaxwellFormat::Alu3In;

    case 0x12: // IADD
    case 0x13: // ISUB
    case 0x14: // IMUL
    case 0x15: // IABS
    case 0x16: // INEG
        return MaxwellFormat::Alu2In;

    case 0x17: // ISETP
        return MaxwellFormat::ISetp;

    case 0x18: // I2F
    case 0x19: // I2I
    case 0x1A: // IMNMX
        return MaxwellFormat::Alu2In;

    case 0x1B: // LEA
        return MaxwellFormat::Alu2In;

    case 0x1C: // POPC
    case 0x1D: // FLO
    case 0x1E: // SHL
    case 0x1F: // SHR
        return MaxwellFormat::Alu1In;

    // ── Type conversion ────────────────────────────
    case 0x20: // F2F
    case 0x21: // F2I
    case 0x22: // F2F_C
        return MaxwellFormat::Alu2In;

    // ── Compare ───────────────────────────────────
    case 0x30: // FSETP
        return MaxwellFormat::FSetp;

    case 0x31: // FSET
        return MaxwellFormat::Alu2In;

    // ── Memory load ───────────────────────────────
    case 0x40: // LD (generic)
    case 0x41: // LG (global)
    case 0x42: // LL (local)
    case 0x43: // LDS (shared)
    case 0x44: // LDC (constant)
        return MaxwellFormat::MemoryLoad;

    // ── Memory store ──────────────────────────────
    case 0x50: // ST (generic)
    case 0x51: // STG (global)
    case 0x52: // STL (local)
    case 0x53: // STS (shared)
        return MaxwellFormat::MemoryStore;

    // ── Texture ──────────────────────────────────
    case 0x60: // TEX
    case 0x61: // TEXS
    case 0x62: // TLDS
    case 0x63: // TLD4
    case 0x64: // TXQ
        return MaxwellFormat::Texture;

    // ── Special ──────────────────────────────────
    case 0x70: // S2R
        return MaxwellFormat::S2R;

    case 0x71: // R2P
    case 0x72: // P2R
        return MaxwellFormat::PredicateReg;

    case 0x73: // PRMT
        return MaxwellFormat::Alu3In;

    case 0x74: // BFE
    case 0x75: // BFI
        return MaxwellFormat::Alu3In;

    // ── Control flow ─────────────────────────────
    case 0x80: // EXIT
        return MaxwellFormat::Exit;

    case 0x81: // BRA
    case 0x82: // CALL
        return MaxwellFormat::Branch;

    case 0x83: // SSY
    case 0x84: // SYNC
        return MaxwellFormat::Sync;

    case 0x85: // RET
        return MaxwellFormat::Exit;

    case 0x86: // KIL
    case 0x87: // CONT
    case 0x88: // BREAK
        return MaxwellFormat::Exit;

    default:
        return MaxwellFormat::Unknown;
    }
}

// ═══════════════════════════════════════════════════════════
// Opcode mapping
// ═══════════════════════════════════════════════════════════

ShaderOpcode MaxwellDecoder::DecodeOpcode(u32 opcode_id, MaxwellFormat fmt) const {
    switch (fmt) {
    case MaxwellFormat::Alu3In:
        switch (opcode_id) {
        case 0x00: return ShaderOpcode::FMAD;
        case 0x10: return ShaderOpcode::IADD3;
        case 0x11: return ShaderOpcode::IMAD;
        case 0x73: return ShaderOpcode::PRMT;
        case 0x74: return ShaderOpcode::BFE;
        case 0x75: return ShaderOpcode::BFI;
        default:   return ShaderOpcode::FMAD;
        }

    case MaxwellFormat::Alu2In:
        switch (opcode_id) {
        case 0x01: return ShaderOpcode::FADD;
        case 0x02: return ShaderOpcode::FMUL;
        case 0x03: return ShaderOpcode::FMAX;
        case 0x04: return ShaderOpcode::FMIN;
        case 0x05: return ShaderOpcode::FSAT;
        case 0x12: return ShaderOpcode::IADD;
        case 0x13: return ShaderOpcode::ISUB;
        case 0x14: return ShaderOpcode::IMUL;
        case 0x15: return ShaderOpcode::IABS;
        case 0x16: return ShaderOpcode::INEG;
        case 0x18: return ShaderOpcode::I2F;
        case 0x19: return ShaderOpcode::I2I;
        case 0x1A: return ShaderOpcode::IMNMX;
        case 0x1B: return ShaderOpcode::LEA;
        case 0x20: return ShaderOpcode::F2F;
        case 0x21: return ShaderOpcode::F2I;
        case 0x22: return ShaderOpcode::F2F_C;
        case 0x31: return ShaderOpcode::FSET;
        default:   return ShaderOpcode::FADD;
        }

    case MaxwellFormat::Alu1In:
        switch (opcode_id) {
        case 0x06: return ShaderOpcode::MUFU;
        case 0x07: return ShaderOpcode::FRCP;
        case 0x08: return ShaderOpcode::FRSQ;
        case 0x09: return ShaderOpcode::FSQRT;
        case 0x0A: return ShaderOpcode::FEX2;
        case 0x0B: return ShaderOpcode::FLG2;
        case 0x0C: return ShaderOpcode::FSIN;
        case 0x0D: return ShaderOpcode::FCOS;
        case 0x1C: return ShaderOpcode::POPC;
        case 0x1D: return ShaderOpcode::FLO;
        case 0x1E: return ShaderOpcode::SHL;
        case 0x1F: return ShaderOpcode::SHR;
        default:   return ShaderOpcode::MUFU;
        }

    case MaxwellFormat::ISetp:
        return ShaderOpcode::ISETP;

    case MaxwellFormat::FSetp:
        return ShaderOpcode::FSETP;

    case MaxwellFormat::MemoryLoad:
        switch (opcode_id) {
        case 0x40: return ShaderOpcode::LD;
        case 0x41: return ShaderOpcode::LG;
        case 0x42: return ShaderOpcode::LL;
        case 0x43: return ShaderOpcode::LDS;
        case 0x44: return ShaderOpcode::LDC;
        default:   return ShaderOpcode::LD;
        }

    case MaxwellFormat::MemoryStore:
        switch (opcode_id) {
        case 0x50: return ShaderOpcode::ST;
        case 0x51: return ShaderOpcode::STG;
        case 0x52: return ShaderOpcode::STL;
        case 0x53: return ShaderOpcode::STS;
        default:   return ShaderOpcode::ST;
        }

    case MaxwellFormat::Texture:
        switch (opcode_id) {
        case 0x60: return ShaderOpcode::TEX;
        case 0x61: return ShaderOpcode::TEXS;
        case 0x62: return ShaderOpcode::TLDS;
        case 0x63: return ShaderOpcode::TLD4;
        case 0x64: return ShaderOpcode::TXQ;
        default:   return ShaderOpcode::TEX;
        }

    case MaxwellFormat::S2R:
        return ShaderOpcode::S2R;

    case MaxwellFormat::PredicateReg:
        switch (opcode_id) {
        case 0x71: return ShaderOpcode::R2P;
        case 0x72: return ShaderOpcode::P2R;
        default:   return ShaderOpcode::R2P;
        }

    case MaxwellFormat::Branch:
        switch (opcode_id) {
        case 0x81: return ShaderOpcode::BRA;
        case 0x82: return ShaderOpcode::CALL;
        default:   return ShaderOpcode::BRA;
        }

    case MaxwellFormat::Sync:
        switch (opcode_id) {
        case 0x83: return ShaderOpcode::SSY;
        case 0x84: return ShaderOpcode::SYNC;
        default:   return ShaderOpcode::SSY;
        }

    case MaxwellFormat::Exit:
        if (opcode_id == 0x85) return ShaderOpcode::RET;
        if (opcode_id == 0x86) return ShaderOpcode::KIL;
        if (opcode_id == 0x87) return ShaderOpcode::CONT;
        if (opcode_id == 0x88) return ShaderOpcode::BREAK;
        return ShaderOpcode::EXIT;

    default:
        return ShaderOpcode::NOP;
    }
}

// ═══════════════════════════════════════════════════════════
// Instruction format decoders
// ═══════════════════════════════════════════════════════════

DecodedInst MaxwellDecoder::DecodeAluFormat(u64 raw0, u64 raw1, u32 pc, MaxwellFormat fmt) {
    DecodedInst inst;
    inst.raw[0] = raw0;
    inst.raw[1] = raw1;
    inst.pc = pc;
    inst.fmt = fmt;

    u32 opcode_id = Bits(raw0, 12, 5);
    inst.opcode = DecodeOpcode(opcode_id, fmt);
    inst.size = 8;

    // Common ALU field layout (varies by format):
    //   bits[19:13] = src0
    //   bits[26:20] = src1
    //   bits[33:27] = src2 (three-input only)
    //   bits[35:33] = pred
    //   bits[44:39] = dest
    //   bits[49:45] = neg/abs modifiers

    switch (fmt) {
    case MaxwellFormat::Alu3In: {
        inst.field.gpr_src0 = Bits(raw0, 19, 13);
        inst.field.gpr_src1 = Bits(raw0, 26, 20);
        inst.field.gpr_src2 = Bits(raw0, 33, 27);
        inst.field.gpr_dest = Bits(raw0, 44, 39);
        inst.field.neg_src0 = Bit(raw0, 45);
        inst.field.neg_src1 = Bit(raw0, 46);
        inst.field.abs_src0 = Bit(raw0, 47);
        inst.field.abs_src1 = Bit(raw0, 48);
        inst.field.saturate = Bit(raw0, 49);
        inst.field.src_count = 3;
        break;
    }
    case MaxwellFormat::Alu2In: {
        inst.field.gpr_src0 = Bits(raw0, 19, 13);
        inst.field.gpr_src1 = Bits(raw0, 26, 20);
        inst.field.gpr_dest = Bits(raw0, 44, 39);
        inst.field.neg_src0 = Bit(raw0, 45);
        inst.field.neg_src1 = Bit(raw0, 46);
        inst.field.abs_src0 = Bit(raw0, 47);
        inst.field.saturate = Bit(raw0, 49);
        inst.field.src_count = 2;

        // For unary ops, only use src0
        if (fmt == MaxwellFormat::Alu1In) {
            inst.field.src_count = 1;
        }
        break;
    }
    case MaxwellFormat::Alu1In: {
        inst.field.gpr_src0 = Bits(raw0, 19, 13);
        inst.field.gpr_dest = Bits(raw0, 44, 39);
        inst.field.src_count = 1;

        // Extract MUFU sub-op
        if (inst.opcode == ShaderOpcode::MUFU) {
            inst.field.mufu_op = static_cast<MufuOp>(Bits(raw0, 54, 51));
        }
        break;
    }
    case MaxwellFormat::ISetp:
    case MaxwellFormat::FSetp: {
        inst.field.gpr_src0 = Bits(raw0, 19, 13);
        inst.field.gpr_src1 = Bits(raw0, 26, 20);
        inst.field.pred_dest = Bits(raw0, 42, 40);
        inst.field.pred_src  = Bits(raw0, 35, 33);
        inst.field.neg_src0  = Bit(raw0, 45);
        inst.field.neg_src1  = Bit(raw0, 46);
        inst.field.src_count = 2;
        inst.field.sets_cc   = true;
        break;
    }
    default:
        break;
    }

    // Mark register usage
    inst.field.is_float = (fmt == MaxwellFormat::Alu2In ||
                           fmt == MaxwellFormat::Alu3In ||
                           fmt == MaxwellFormat::Alu1In ||
                           fmt == MaxwellFormat::FSetp);

    return inst;
}

DecodedInst MaxwellDecoder::DecodeMemoryFormat(u64 raw0, u64 raw1, u32 pc) {
    DecodedInst inst;
    inst.raw[0] = raw0;
    inst.raw[1] = raw1;
    inst.pc = pc;
    inst.size = 16;
    inst.fmt = MaxwellFormat::MemoryLoad;

    u32 opcode_id = Bits(raw0, 12, 5);
    // Determine if load or store based on opcode
    if (opcode_id >= 0x50) {
        inst.fmt = MaxwellFormat::MemoryStore;
        inst.opcode = DecodeOpcode(opcode_id, MaxwellFormat::MemoryStore);
    } else {
        inst.opcode = DecodeOpcode(opcode_id, MaxwellFormat::MemoryLoad);
    }

    // Memory instruction layout:
    //   bits[19:13] = dest/src GPR
    //   bits[44:39] = address GPR
    //   bits[34:27] = offset
    // Higher bits encode memory space and cache ops

    inst.field.gpr_dest = Bits(raw0, 19, 13);
    inst.field.mem_offset = Bits(raw0, 34, 27);
    inst.field.gpr_src0 = Bits(raw0, 44, 39);   // Address register

    // Memory space from bits [47:45] of raw1
    u32 mem_type = Bits(raw1, 47, 45);
    switch (mem_type) {
    case 0: inst.field.mem_space = MemorySpace::Global;   break;
    case 1: inst.field.mem_space = MemorySpace::Shared;   break;
    case 2: inst.field.mem_space = MemorySpace::Local;    break;
    case 3: inst.field.mem_space = MemorySpace::Constant; break;
    default: inst.field.mem_space = MemorySpace::Generic; break;
    }

    inst.field.src_count = 1;

    return inst;
}

DecodedInst MaxwellDecoder::DecodeTextureFormat(u64 raw0, u64 raw1, u32 pc) {
    DecodedInst inst;
    inst.raw[0] = raw0;
    inst.raw[1] = raw1;
    inst.pc = pc;
    inst.size = 16;
    inst.fmt = MaxwellFormat::Texture;

    u32 opcode_id = Bits(raw0, 12, 5);
    inst.opcode = DecodeOpcode(opcode_id, MaxwellFormat::Texture);

    // Texture instruction layout:
    //   bits[19:13] = dest GPR
    //   bits[44:39] = texture index
    //   bits[49:44] = sampler index (in raw1)
    //   bits[33:27] = source component GPR

    inst.field.gpr_dest = Bits(raw0, 19, 13);
    inst.field.tex_index = Bits(raw0, 44, 39);
    inst.field.sampler_idx = Bits(raw1, 49, 44);
    inst.field.gpr_src0 = Bits(raw0, 33, 27);
    inst.field.src_count = 1;

    return inst;
}

DecodedInst MaxwellDecoder::DecodeBranchFormat(u64 raw0, u64 raw1, u32 pc,
                                                   MaxwellFormat fmt) {
    DecodedInst inst;
    inst.raw[0] = raw0;
    inst.raw[1] = raw1;
    inst.pc = pc;
    inst.size = 8;
    inst.fmt = fmt;

    u32 opcode_id = Bits(raw0, 12, 5);
    inst.opcode = DecodeOpcode(opcode_id, fmt);

    // Branch/Sync layout:
    //   bits[26:13] = branch target offset (14-bit signed, in 8-byte units)
    //   bits[35:33] = predicate source
    s32 offset = SignExtend(Bits(raw0, 26, 13), 14);
    inst.field.branch_target = pc + 8 + offset * 8;
    inst.field.pred_src = Bits(raw0, 35, 33);
    inst.field.src_count = 0;

    return inst;
}

DecodedInst MaxwellDecoder::DecodeS2RFormat(u64 raw0, u32 pc) {
    DecodedInst inst;
    inst.raw[0] = raw0;
    inst.pc = pc;
    inst.size = 8;
    inst.fmt = MaxwellFormat::S2R;
    inst.opcode = ShaderOpcode::S2R;

    // S2R layout:
    //   bits[19:13] = dest GPR
    //   bits[44:39] = special register index

    inst.field.gpr_dest = Bits(raw0, 19, 13);
    inst.field.gpr_src0 = Bits(raw0, 44, 39); // Special register index
    inst.field.src_count = 0;

    return inst;
}

DecodedInst MaxwellDecoder::DecodeExitFormat(u64 raw0, u32 pc) {
    DecodedInst inst;
    inst.raw[0] = raw0;
    inst.pc = pc;
    inst.size = 8;
    inst.fmt = MaxwellFormat::Exit;

    u32 opcode_id = Bits(raw0, 12, 5);
    inst.opcode = DecodeOpcode(opcode_id, MaxwellFormat::Exit);
    inst.field.src_count = 0;

    return inst;
}

// ═══════════════════════════════════════════════════════════
// Single instruction decode
// ═══════════════════════════════════════════════════════════

DecodedInst MaxwellDecoder::DecodeOne(const u8* data, u32 size, u32 pc,
                                       DecoderContext* ctx) {
    DecodedInst inst;
    inst.pc = pc;

    if (pc + 8 > size) {
        LogError("Truncated instruction at offset 0x%x", pc);
        return inst;
    }

    u64 raw0 = 0;
    std::memcpy(&raw0, data + pc, 8);

    u32 opcode_id = Bits(raw0, 12, 5);
    MaxwellFormat fmt = DetectFormat(opcode_id, raw0);

    // Use raw1 for memory/texture instructions that need it
    u64 raw1 = 0;
    if (pc + 16 <= size &&
        (fmt == MaxwellFormat::MemoryLoad ||
         fmt == MaxwellFormat::MemoryStore ||
         fmt == MaxwellFormat::Texture)) {
        std::memcpy(&raw1, data + pc + 8, 8);
        inst.size = 16;
    }

    switch (fmt) {
    case MaxwellFormat::Alu3In:
    case MaxwellFormat::Alu2In:
    case MaxwellFormat::Alu1In:
    case MaxwellFormat::ISetp:
    case MaxwellFormat::FSetp:
        inst = DecodeAluFormat(raw0, raw1, pc, fmt);
        break;

    case MaxwellFormat::MemoryLoad:
    case MaxwellFormat::MemoryStore:
        inst = DecodeMemoryFormat(raw0, raw1, pc);
        break;

    case MaxwellFormat::Texture:
        inst = DecodeTextureFormat(raw0, raw1, pc);
        break;

    case MaxwellFormat::Branch:
    case MaxwellFormat::Sync:
        inst = DecodeBranchFormat(raw0, raw1, pc, fmt);
        break;

    case MaxwellFormat::S2R:
        inst = DecodeS2RFormat(raw0, pc);
        break;

    case MaxwellFormat::Exit:
        inst = DecodeExitFormat(raw0, pc);
        break;

    case MaxwellFormat::PredicateReg:
        inst = DecodeS2RFormat(raw0, pc);
        inst.fmt = MaxwellFormat::PredicateReg;
        inst.opcode = DecodeOpcode(Bits(raw0, 12, 5), MaxwellFormat::PredicateReg);
        inst.field.src_count = 0;
        break;

    default:
        inst.fmt = MaxwellFormat::Unknown;
        inst.opcode = ShaderOpcode::NOP;
        LogError("Unknown instruction at 0x%x: raw=0x%016llx", pc, raw0);
        break;
    }

    // Track register usage
    if (ctx) {
        ctx->UseGpr(inst.field.gpr_dest);
        ctx->UseGpr(inst.field.gpr_src0);
        ctx->UseGpr(inst.field.gpr_src1);
        ctx->UseGpr(inst.field.gpr_src2);
        ctx->UsePred(inst.field.pred_src);
        ctx->UsePred(inst.field.pred_dest);
    }

    return inst;
}

// ═══════════════════════════════════════════════════════════
// Full program decode
// ═══════════════════════════════════════════════════════════

ShaderProgram MaxwellDecoder::Decode(const u8* data, u32 size,
                                      ShaderStage stage) {
    ShaderProgram program;
    program.stage = stage;
    program.program_size = size;
    program.hash = ShaderProgram::CalculateHash(data, size);

    DecoderContext ctx;
    ctx.data = data;
    ctx.size = size;

    // Decode instructions
    while (ctx.offset < size) {
        DecodedInst di = DecodeOne(data, size, ctx.offset, &ctx);

        if (di.fmt == MaxwellFormat::Unknown) {
            ctx.offset += 8;
            continue;
        }

        // Add to program
        ShaderInstruction ir = di.ToIr();
        program.AddInst(ir);

        // Check for EXIT
        if (di.opcode == ShaderOpcode::EXIT) {
            ctx.offset += di.size;
            break;
        }

        ctx.offset += di.size;
    }

    // Gather metadata
    program.num_gprs_used = ctx.CountGprsUsed();
    program.num_preds_used = ctx.CountPredsUsed();

    // Check for texture usage
    for (const auto& inst : program.instructions) {
        if (inst.opcode == ShaderOpcode::TEX ||
            inst.opcode == ShaderOpcode::TEXS ||
            inst.opcode == ShaderOpcode::TLDS) {
            program.uses_textures = true;
        }
        if (inst.opcode == ShaderOpcode::LD  || inst.opcode == ShaderOpcode::LG  ||
            inst.opcode == ShaderOpcode::LL  || inst.opcode == ShaderOpcode::LDS ||
            inst.opcode == ShaderOpcode::ST  || inst.opcode == ShaderOpcode::STG ||
            inst.opcode == ShaderOpcode::STL || inst.opcode == ShaderOpcode::STS) {
            program.uses_varyings = true;
        }
    }

    errors_ = ctx.errors;

    LOG_INFO("Decoded %s: %zu instructions, %u GPRs, hash=0x%016llx",
             ShaderStageName(stage), program.instructions.size(),
             program.num_gprs_used, program.hash);

    return program;
}

// ═══════════════════════════════════════════════════════════
// DecodedInst → ShaderInstruction conversion
// ═══════════════════════════════════════════════════════════

ShaderInstruction DecodedInst::ToIr() const {
    ShaderInstruction ir;
    ir.opcode = opcode;
    ir.pc = pc;
    ir.src_count = field.src_count;

    // Destination (GPR 0 is a valid register on Maxwell)
    // Set dest for any opcode that writes a register; always set for control flow ops.
    if (opcode == ShaderOpcode::EXIT || field.src_count > 0 ||
        field.gpr_dest != 0) {
        ir.dest = ShaderOperand::Gpr(field.gpr_dest);
        ir.dest.saturate = field.saturate;
        ir.dest.data_type = field.is_float ? DataType::F32 : DataType::U32;
    }

    // Source operands (all GPRs including r0 are valid)
    // Use field.src_count directly since ir.src_count may be overridden by
    // texture/branch/S2R special case blocks below.
    if (field.src_count >= 1) {
        ir.src[0] = ShaderOperand::Gpr(field.gpr_src0);
        ir.src[0].negate = field.neg_src0;
        ir.src[0].absolute = field.abs_src0;
        ir.src[0].data_type = field.is_float ? DataType::F32 : DataType::U32;
    }

    if (field.src_count >= 2) {
        ir.src[1] = ShaderOperand::Gpr(field.gpr_src1);
        ir.src[1].negate = field.neg_src1;
        ir.src[1].absolute = field.abs_src1;
        ir.src[1].data_type = field.is_float ? DataType::F32 : DataType::U32;
    }

    if (field.src_count >= 3) {
        ir.src[2] = ShaderOperand::Gpr(field.gpr_src2);
        ir.src[2].data_type = field.is_float ? DataType::F32 : DataType::U32;
    }

    // Predicate guard (from bits [28:26] — pred_src)
    if (field.pred_src != 0) {
        ir.pred_guard = true;
        ir.pred_guard_index = field.pred_src;
    }

    // Memory space
    ir.mem_space = field.mem_space;

    // MUFU sub-op
    if (opcode == ShaderOpcode::MUFU && field.src_count >= 1) {
        ir.src[0].mufu_op = field.mufu_op;
    }

    // Branch target (BRA, CALL, SSY, SYNC)
    if (opcode == ShaderOpcode::BRA   || opcode == ShaderOpcode::CALL ||
        opcode == ShaderOpcode::SSY   || opcode == ShaderOpcode::SYNC) {
        ir.src[0] = ShaderOperand::Label(field.branch_target / 8);
        ir.src_count = 1;
    }

    // Texture info
    if (opcode == ShaderOpcode::TEX || opcode == ShaderOpcode::TEXS ||
        opcode == ShaderOpcode::TLDS || opcode == ShaderOpcode::TLD4) {
        ir.dest = ShaderOperand::Gpr(field.gpr_dest);
        ir.src[0] = ShaderOperand::Gpr(field.gpr_src0);  // Coordinate
        ir.src[1] = ShaderOperand::Texture(field.tex_index, field.sampler_idx);
        ir.src_count = 2;
    }

    // Special register
    if (opcode == ShaderOpcode::S2R) {
        ir.dest = ShaderOperand::Gpr(field.gpr_dest);
        ir.src[0] = ShaderOperand::Special(static_cast<SpecialReg>(field.gpr_src0));
        ir.src_count = 1;
    }

    return ir;
}

// ═══════════════════════════════════════════════════════════
// Guest memory shader extraction
// ═══════════════════════════════════════════════════════════

std::vector<u8> ShaderExtract::ExtractShader(const u8* program_region,
                                              u64 region_size,
                                              u64 shader_offset,
                                              u32 max_size) {
    std::vector<u8> result;

    if (!program_region || region_size == 0) {
        LOG_ERROR("ShaderExtract: invalid program region");
        return result;
    }

    if (shader_offset >= region_size) {
        LOG_ERROR("ShaderExtract: offset 0x%llx out of range", shader_offset);
        return result;
    }

    // Clamp to region + max size
    u32 available = (u32)std::min<u64>(region_size - shader_offset, max_size);

    // Scan for EXIT instruction (opcode 0x80 in bits [12:5])
    // Most shaders end with EXIT at a predictable spot
    u32 end_offset = available;

    for (u32 i = 0; i + 8 <= available; i += 8) {
        u64 raw = 0;
        std::memcpy(&raw, program_region + shader_offset + i, 8);
        u32 opcode = (u32)((raw >> 5) & 0xFF);
        if (opcode == 0x80) {
            end_offset = i + 8;
            break;
        }
    }

    result.resize(end_offset);
    std::memcpy(result.data(), program_region + shader_offset, end_offset);

    LOG_INFO("ShaderExtract: offset=0x%llx, size=%u (out of %u available)",
             shader_offset, end_offset, available);

    return result;
}

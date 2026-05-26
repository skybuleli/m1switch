#include "gpu/shader/ShaderIr.h"

// ── Operand to string ───────────────────────────────────────
std::string ShaderOperand::ToString() const {
    char buf[128] = {};

    switch (type) {
    case OperandType::None:
        return "none";

    case OperandType::GPR:
        snprintf(buf, sizeof(buf), "%sr%u%s%s",
                 negate ? "-" : "",
                 gpr.reg_index,
                 absolute ? ".abs" : "",
                 saturate ? ".sat" : "");
        return buf;

    case OperandType::Predicate:
        snprintf(buf, sizeof(buf), "%sp%u",
                 pred.negate ? "!" : "",
                 pred.pred_index);
        return buf;

    case OperandType::FloatImm:
        snprintf(buf, sizeof(buf), "%f", imm_f32);
        return buf;

    case OperandType::IntImm:
        if (data_type == DataType::S32)
            snprintf(buf, sizeof(buf), "%d", imm_s32);
        else
            snprintf(buf, sizeof(buf), "0x%x", imm_u32);
        return buf;

    case OperandType::BoolImm:
        return imm_bool ? "true" : "false";

    case OperandType::Cbuf:
        snprintf(buf, sizeof(buf), "cb[%u][0x%x]",
                 cbuf.cbuf_index, cbuf.offset);
        return buf;

    case OperandType::Attribute:
        snprintf(buf, sizeof(buf), "attr[%u].%c",
                 attr.attr_index, 'x' + attr.component);
        return buf;

    case OperandType::Varying:
        snprintf(buf, sizeof(buf), "varying[%u].%c",
                 varying.varying_index, 'x' + varying.component);
        return buf;

    case OperandType::SpecialReg:
        switch (special_reg) {
        case SpecialReg::LaneId:       return "laneid";
        case SpecialReg::ThreadIdX:    return "tid.x";
        case SpecialReg::ThreadIdY:    return "tid.y";
        case SpecialReg::ThreadIdZ:    return "tid.z";
        case SpecialReg::BlockSizeX:   return "ntid.x";
        case SpecialReg::BlockSizeY:   return "ntid.y";
        case SpecialReg::BlockSizeZ:   return "ntid.z";
        case SpecialReg::BlockIdX:     return "ctaid.x";
        case SpecialReg::BlockIdY:     return "ctaid.y";
        case SpecialReg::BlockIdZ:     return "ctaid.z";
        case SpecialReg::InvocationId: return "invocationid";
        case SpecialReg::PrimitiveId:  return "primitiveid";
        default: return "s2r_unknown";
        }

    case OperandType::Label:
        snprintf(buf, sizeof(buf), "label_%u", label);
        return buf;

    default:
        return "?";
    }
}

// ── Instruction to string ───────────────────────────────────
std::string ShaderInstruction::ToString() const {
    std::string result = ShaderOpcodeName(opcode);

    // Predicate guard
    if (pred_guard) {
        result += pred_guard_negate ? "@!p" : "@p";
        result += std::to_string(pred_guard_index);
    }

    // Destination
    if (dest.type != OperandType::None) {
        result += " " + dest.ToString();
    }

    // Sources
    if (src_count > 0) {
        result += ", " + src[0].ToString();
    }
    if (src_count > 1) {
        result += ", " + src[1].ToString();
    }
    if (src_count > 2) {
        result += ", " + src[2].ToString();
    }

    // Memory qualifiers
    if (opcode == ShaderOpcode::LD || opcode == ShaderOpcode::ST ||
        opcode == ShaderOpcode::LG || opcode == ShaderOpcode::STG ||
        opcode == ShaderOpcode::LL || opcode == ShaderOpcode::STL) {
        switch (mem_space) {
        case MemorySpace::Global:   result += ".global"; break;
        case MemorySpace::Shared:   result += ".shared"; break;
        case MemorySpace::Local:    result += ".local"; break;
        case MemorySpace::Constant: result += ".const"; break;
        default: break;
        }
    }

    return result;
}

// ── Program hash ─────────────────────────────────────────────
u64 ShaderProgram::CalculateHash(const u8* data, u32 size) {
    // Simple FNV-1a 64-bit hash
    u64 hash = 0xCBF29CE484222325ULL;
    for (u32 i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

// ── Dump program ────────────────────────────────────────────
void ShaderProgram::Dump() const {
    LOG_INFO("=== ShaderProgram (%s) ===", ShaderStageName(stage));
    LOG_INFO("  Offset: 0x%x, Size: %u bytes", program_offset, program_size);
    LOG_INFO("  Hash: 0x%016llx", hash);
    LOG_INFO("  Instructions: %zu", instructions.size());
    LOG_INFO("  GPRs used: %u, Preds used: %u",
             num_gprs_used, num_preds_used);

    for (size_t i = 0; i < instructions.size(); i++) {
        LOG_INFO("  [0x%04x] %s",
                 instructions[i].pc,
                 instructions[i].ToString().c_str());
    }
    LOG_INFO("=== End %s ===", ShaderStageName(stage));
}

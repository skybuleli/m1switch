#pragma once

#include "gpu/shader/ShaderIr.h"

#include <span>
#include <vector>
#include <functional>
#include <unordered_map>

// ═══════════════════════════════════════════════════════════
// Maxwell ISA Decoder
// ═══════════════════════════════════════════════════════════
//
// Decodes Maxwell shader binaries (64-bit instructions)
// into the M1Switch intermediate representation (ShaderIr).
//
// Maxwell instruction formats:
//   - Most ALU ops are 64 bits (8 bytes)
//   - Memory ops (LD/ST) can be 64 or 128 bits
//   - Branch instructions (BRA/SSY) are 64 bits
//
// Reference: envytools (gm107.c), Ryujinx shader decoder

// ── Maxwell instruction format IDs ───────────────────────────
// These identify the specific bit layout of each instruction.
enum class MaxwellFormat : u8 {
    Unknown = 0,

    // ALU: three-input (FMAD, IMAD, etc.)
    //   op[5:12], src0[13:19], src1[20:26], src2[27:33], pred[33:35], ...
    Alu3In,

    // ALU: two-input (FADD, FMUL, IADD, etc.)
    //   op[5:12], src0[13:19], src1[20:26], ...
    Alu2In,

    // ALU: one-input (MUFU, FRCP, RSQ, etc.)
    //   op[5:12], src[13:19], ...
    Alu1In,

    // Integer compare (ISETP)
    ISetp,

    // Float compare (FSETP)
    FSetp,

    // Load (LD.LG, LD.S, etc.)
    MemoryLoad,

    // Store (ST, STG, STS, etc.)
    MemoryStore,

    // Texture sample (TEX, TEXS, etc.)
    Texture,

    // Special register (S2R)
    S2R,

    // Predicate register (R2P, P2R)
    PredicateReg,

    // Branch (BRA, CALL, etc.)
    Branch,

    // Sync (SSY, SYNC)
    Sync,

    // Exit
    Exit,

    // Shader header (start of program)
    Header,
};

// ── Decoded instruction ──────────────────────────────────────
struct DecodedInst {
    u64 raw[2] = {};       // Raw instruction words
    u32  pc = 0;           // Program offset
    u32  size = 8;         // Size in bytes (usually 8)

    MaxwellFormat fmt = MaxwellFormat::Unknown;
    ShaderOpcode opcode = ShaderOpcode::NOP;

    // Decoded fields
    struct Field {
        u32 gpr_dest    : 8;   // Destination GPR
        u32 gpr_src0    : 8;   // Source 0 GPR
        u32 gpr_src1    : 8;   // Source 1 GPR
        u32 gpr_src2    : 8;   // Source 2 GPR
        u32 pred_src    : 3;   // Source predicate
        u32 pred_dest   : 3;   // Destination predicate
        u32 imm_u32     : 32;  // Immediate (if present)
        f32  imm_f32;          // Float immediate union
        s32  imm_s32;          // Signed immediate union
        u32 cbuf_index  : 8;   // Constant buffer index
        u32 cbuf_offset : 16;  // Constant buffer offset
        u32 attr_index  : 8;   // Attribute index
        u32 tex_index   : 8;   // Texture index
        u32 sampler_idx : 8;   // Sampler index
        u32 mem_offset  : 20;  // Memory offset
        u32 branch_target;       // Branch target (absolute PC in bytes)
        u8  src_count   : 3;   // Source count
        bool neg_src0   : 1;   // Negate source 0
        bool neg_src1   : 1;   // Negate source 1
        bool abs_src0   : 1;   // Absolute source 0
        bool abs_src1   : 1;   // Absolute source 1
        bool saturate   : 1;   // Saturate output
        bool sets_cc    : 1;   // Sets condition code
        bool is_float   : 1;   // Float operation
        MufuOp mufu_op  : 4;   // MUFU sub-op
        MemorySpace mem_space; // Memory space
    } field = {};

    // Convert to ShaderIr instruction
    ShaderInstruction ToIr() const;
};

// ── Decoder context ─────────────────────────────────────────
// Holds per-shader state during decoding.
struct DecoderContext {
    const u8* data = nullptr;   // Raw shader binary
    u32 size = 0;               // Size in bytes
    u32 offset = 0;             // Current decode offset

    // Track register usage
    bool gpr_used[256] = {};
    bool pred_used[8] = {};

    // Label resolution (branch targets)
    std::unordered_map<u32, u32> label_map;  // offset → label_id

    // Errors
    std::vector<std::string> errors;

    // Mark GPR as used
    void UseGpr(u8 index) { if (index < 256) gpr_used[index] = true; }
    void UsePred(u8 index) { if (index < 8) pred_used[index] = true; }

    u32 CountGprsUsed() const {
        u32 count = 0;
        for (bool used : gpr_used) if (used) count++;
        return count;
    }

    u32 CountPredsUsed() const {
        u32 count = 0;
        for (bool used : pred_used) if (used) count++;
        return count;
    }
};

// ── Maxwell decoder ─────────────────────────────────────────
class MaxwellDecoder {
public:
    MaxwellDecoder() = default;

    // Decode a complete shader program from raw binary
    // Returns a ShaderProgram with decoded instructions
    ShaderProgram Decode(const u8* data, u32 size, ShaderStage stage);

    // Decode a single instruction at the given offset
    DecodedInst DecodeOne(const u8* data, u32 size, u32 pc,
                          DecoderContext* ctx = nullptr);

    // Get the last error messages
    const std::vector<std::string>& GetErrors() const { return errors_; }

    // Check if decode was successful
    bool HasErrors() const { return !errors_.empty(); }

private:
    std::vector<std::string> errors_;

    // Internal decoding helpers
    void LogError(const char* fmt, ...);

    // Opcode decode by format
    ShaderOpcode DecodeOpcode(u32 opcode_id, MaxwellFormat fmt) const;
    MaxwellFormat DetectFormat(u32 opcode_id, u64 raw) const;

    // Field extractors for different instruction formats
    DecodedInst DecodeAluFormat(u64 raw0, u64 raw1, u32 pc, MaxwellFormat fmt);
    DecodedInst DecodeMemoryFormat(u64 raw0, u64 raw1, u32 pc);
    DecodedInst DecodeTextureFormat(u64 raw0, u64 raw1, u32 pc);
    DecodedInst DecodeBranchFormat(u64 raw0, u64 raw1, u32 pc, MaxwellFormat fmt);
    DecodedInst DecodeS2RFormat(u64 raw0, u32 pc);
    DecodedInst DecodeExitFormat(u64 raw0, u32 pc);
};

// ── Guest memory shader extraction ──────────────────────────
// Helper to extract shader binaries from the guest's program region.
namespace ShaderExtract {

    // Extract a shader binary from guest memory at program_region + offset
    // Returns the shader data, or empty span on failure.
    // The shader size is determined by scanning for EXIT instructions.
    std::vector<u8> ExtractShader(const u8* program_region, u64 region_size,
                                   u64 shader_offset, u32 max_size = 4096);

} // namespace ShaderExtract


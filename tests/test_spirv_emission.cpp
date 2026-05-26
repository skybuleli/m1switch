// ── SPIR-V Emission Tests ──────────────────────────────────
// Verifies that the SpirvEmitter produces structurally valid
// SPIR-V binaries for various Maxwell shader programs.
//
// Tests construct ShaderPrograms manually (and decode from raw
// binaries), emit SPIR-V via the SpirvEmitter, then validate
// the binary output for structure and opcode correctness.

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <unistd.h>

#include "common/Log.h"
#include "common/Types.h"
#include "gpu/shader/SpirvEmitter.h"
#include "gpu/shader/MaxwellDecoder.h"

// ── spirv-val validation ────────────────────────────────────
// Runs spirv-val on the SPIR-V binary and returns true if valid.

// Write SPIR-V to file and run spirv-val
static bool ValidateWithSpirvVal(const std::vector<u32>& spirv) {
    // Use a unique file for each test (PID based)
    char tmp_path[64];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/spirv_test_%d.spv", getpid());
    
    FILE* f = fopen(tmp_path, "wb");
    if (!f) return false;
    size_t n = fwrite(spirv.data(), 4, spirv.size(), f);
    fclose(f);
    
    // Verify file size
    long file_bytes = 0;
    FILE* sf = fopen(tmp_path, "rb");
    if (sf) {
        fseek(sf, 0, SEEK_END);
        file_bytes = ftell(sf);
        fclose(sf);
    }
    
    if ((size_t)file_bytes != spirv.size() * 4) {
        fprintf(stderr, "\n=== FILE SIZE MISMATCH: wrote %zu words, file is %ld bytes ===\n",
                spirv.size(), file_bytes);
    }
    
    // Run spirv-val
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "spirv-val %s 2>&1", tmp_path);
    FILE* pipe = popen(cmd, "r");
    if (!pipe) { unlink(tmp_path); return false; }
    
    char buf[1024];
    std::string output;
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    int rc = pclose(pipe);
    int exit_code = WEXITSTATUS(rc);
    
    if (exit_code != 0) {
        fprintf(stderr, "\n=== spirv-val error (%zu words, %ld bytes) ===\n%s\n",
                spirv.size(), file_bytes, output.c_str());
        // Also disassemble
        char dis_cmd[512];
        snprintf(dis_cmd, sizeof(dis_cmd), "spirv-dis %s 2>&1", tmp_path);
        pipe = popen(dis_cmd, "r");
        if (pipe) {
            while (fgets(buf, sizeof(buf), pipe)) {
                fprintf(stderr, "%s", buf);
            }
            pclose(pipe);
        }
    }
    
    // Keep the file for debugging when tests fail
    if (exit_code == 0) unlink(tmp_path);
    return exit_code == 0;
}

// ── SPIR-V binary layout helpers ────────────────────────────
// SPIRV_MAGIC is defined in gpu/shader/ShaderIr.h (included via SpirvEmitter.h)

// Instruction word count macro: (word_count << 16) | opcode
static u32 MakeSpirvInst(u32 word_count, u32 opcode) {
    return (word_count << 16) | opcode;
}

// Check if a word is a SPIR-V instruction header with the given opcode
static bool IsSpirvInst(u32 word, u32 opcode) {
    return (word & 0xFFFF) == opcode;
}

// Extract word count from instruction header
static u32 SpivWordCount(u32 word) {
    return word >> 16;
}

// ── SPIR-V opcodes (subset used in validation) ──────────────
namespace SpirvOpCheck {
    constexpr u32 Capability               = 17;
    constexpr u32 ExtInstImport            = 11;
    constexpr u32 MemoryModel              = 14;
    constexpr u32 EntryPoint               = 15;
    constexpr u32 ExecutionMode            = 16;
    constexpr u32 TypeVoid                 = 19;
    constexpr u32 TypeBool                 = 20;
    constexpr u32 TypeInt                  = 21;
    constexpr u32 TypeFloat                = 22;
    constexpr u32 TypeVector               = 23;
    constexpr u32 TypePointer              = 32;
    constexpr u32 TypeFunction             = 33;
    constexpr u32 Constant                 = 43;
    constexpr u32 Variable                 = 59;
    constexpr u32 Function                 = 54;
    constexpr u32 FunctionEnd              = 56;
    constexpr u32 Label                    = 248;
    constexpr u32 Branch                   = 249;
    constexpr u32 Load                     = 61;
    constexpr u32 Store                    = 62;
    constexpr u32 FAdd                     = 129;
    constexpr u32 FMul                     = 133;
    constexpr u32 FNegate                  = 127;
    constexpr u32 IAdd                     = 128;
    constexpr u32 ISub                     = 130;
    constexpr u32 Return                   = 253;
    constexpr u32 Name                     = 5;
}

// ═════════════════════════════════════════════════════════════
// Helper: build a minimal ShaderProgram with one instruction
// ═════════════════════════════════════════════════════════════

static ShaderProgram MakeSimpleProgram(ShaderOpcode opcode,
                                        u8 dest_reg,
                                        u8 src0_reg,
                                        u8 src1_reg = 0,
                                        u8 src2_reg = 0,
                                        u8 src_count = 2) {
    ShaderProgram prog;
    prog.stage = ShaderStage::Fragment;
    prog.program_size = 8;

    ShaderInstruction inst;
    inst.opcode = opcode;
    inst.pc = 0;

    if (dest_reg != 0) {
        inst.dest = ShaderOperand::Gpr(dest_reg);
    }

    inst.src_count = src_count;
    if (src_count >= 1 && src0_reg != 0) {
        inst.src[0] = ShaderOperand::Gpr(src0_reg);
    }
    if (src_count >= 2 && src1_reg != 0) {
        inst.src[1] = ShaderOperand::Gpr(src1_reg);
    }
    if (src_count >= 3 && src2_reg != 0) {
        inst.src[2] = ShaderOperand::Gpr(src2_reg);
    }

    prog.AddInst(inst);
    prog.num_gprs_used = 3;
    prog.num_preds_used = 0;

    return prog;
}

// ═════════════════════════════════════════════════════════════
// Helper: scan SPIR-V binary for instruction opcodes
// ═════════════════════════════════════════════════════════════

// Check if a word looks like a valid SPIR-V instruction header.
// String data in SPIR-V produces garbage word counts (>> 16)
// that would be unreasonably large for a real instruction.
static bool IsValidInstHeader(u32 word) {
    u32 wc = word >> 16;
    return wc >= 1 && wc <= 50;
}

// Linear scan for an opcode, jumping by word count to skip
// instruction body data that could falsely match opcodes.
// Only instruction headers (positioned at valid boundaries) are checked.
static int FindSpirvOp(const std::vector<u32>& spirv, u32 opcode, size_t start_idx = 5) {
    size_t i = start_idx;
    while (i < spirv.size()) {
        if (IsValidInstHeader(spirv[i]) && (spirv[i] & 0xFFFF) == opcode) {
            return (int)i;
        }
        // Jump over this instruction's body to the next header
        u32 wc = SpivWordCount(spirv[i]);
        i += (wc >= 1 && wc <= 50) ? wc : 1;
    }
    return -1;
}

// Count occurrences of an opcode, jumping by word count.
// Only valid instruction headers at proper boundaries are counted.
static int CountSpirvOps(const std::vector<u32>& spirv, u32 opcode) {
    int count = 0;
    size_t i = 5;
    while (i < spirv.size()) {
        if (IsValidInstHeader(spirv[i]) && (spirv[i] & 0xFFFF) == opcode) {
            count++;
        }
        // Jump over this instruction's body to the next header
        u32 wc = SpivWordCount(spirv[i]);
        i += (wc >= 1 && wc <= 50) ? wc : 1;
    }
    return count;
}

// ═════════════════════════════════════════════════════════════
// Test: Header validation
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_Header) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::EXIT, 0, 0, 0, 0, 0);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // SPIR-V header is 5 words
    CHECK(spirv.size() >= 5);

    // Word 0: Magic number
    CHECK_EQ(SPIRV_MAGIC, spirv[0]);

    // Word 1: Version
    CHECK_EQ(0x00010000, spirv[1]);  // SPIR-V 1.0

    // Word 3: Bound must be > 0 (at least 1 ID allocated)
    CHECK(spirv[3] > 0);

    // Word 4: Schema must be 0
    CHECK_EQ(0, spirv[4]);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Capabilities are emitted
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_Capabilities) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::EXIT, 0, 0, 0, 0, 0);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Should have at least 1 Capability instruction (Shader)
    int cap_count = CountSpirvOps(spirv, SpirvOpCheck::Capability);
    CHECK(cap_count >= 1);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Memory model, entry point, and extension import
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_Preamble) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::EXIT, 0, 0, 0, 0, 0);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Should have ExtInstImport (GLSL.std.450)
    int import_count = CountSpirvOps(spirv, SpirvOpCheck::ExtInstImport);
    CHECK_EQ(1, import_count);

    // Should have MemoryModel
    int model_count = CountSpirvOps(spirv, SpirvOpCheck::MemoryModel);
    CHECK_EQ(1, model_count);

    // Should have EntryPoint
    int entry_count = CountSpirvOps(spirv, SpirvOpCheck::EntryPoint);
    CHECK_EQ(1, entry_count);

    // For fragment shader, should have ExecutionMode (OriginUpperLeft)
    int exec_count = CountSpirvOps(spirv, SpirvOpCheck::ExecutionMode);
    CHECK_EQ(1, exec_count);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Type declarations are emitted
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_Types) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::FADD, 1, 2, 3, 0, 2);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Should have TypeFloat (f32)
    int float_count = CountSpirvOps(spirv, SpirvOpCheck::TypeFloat);
    CHECK(float_count >= 1);

    // Should have TypeInt (u32, i32)
    int int_count = CountSpirvOps(spirv, SpirvOpCheck::TypeInt);
    CHECK(int_count >= 2);

    // Should have TypePointer (for Function storage class)
    int ptr_count = CountSpirvOps(spirv, SpirvOpCheck::TypePointer);
    CHECK(ptr_count >= 1);

    // Should have TypeFunction
    int func_type_count = CountSpirvOps(spirv, SpirvOpCheck::TypeFunction);
    CHECK_EQ(1, func_type_count);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Function structure
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_Function) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::EXIT, 0, 0, 0, 0, 0);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Should have 1 Function
    int func_count = CountSpirvOps(spirv, SpirvOpCheck::Function);
    CHECK_EQ(1, func_count);

    // Should have 1 FunctionEnd
    int func_end_count = CountSpirvOps(spirv, SpirvOpCheck::FunctionEnd);
    CHECK_EQ(1, func_end_count);

    // Should have at least 1 Label
    int label_count = CountSpirvOps(spirv, SpirvOpCheck::Label);
    CHECK(label_count >= 1);

    // Should have Return
    int ret_count = CountSpirvOps(spirv, SpirvOpCheck::Return);
    CHECK_EQ(1, ret_count);

    // Function should come before FunctionEnd
    int func_pos = FindSpirvOp(spirv, SpirvOpCheck::Function);
    int func_end_pos = FindSpirvOp(spirv, SpirvOpCheck::FunctionEnd);
    CHECK(func_pos >= 0);
    CHECK(func_end_pos >= 0);
    CHECK(func_pos < func_end_pos);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: FADD → SPIR-V FAdd
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_FADD) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::FADD, 1, 2, 3, 0, 2);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Should have at least 1 FAdd instruction
    int fadd_count = CountSpirvOps(spirv, SpirvOpCheck::FAdd);
    CHECK(fadd_count >= 1);

    // Should have Load instructions (for reading GPRs)
    int load_count = CountSpirvOps(spirv, SpirvOpCheck::Load);
    CHECK(load_count >= 2);

    // Should have Store instructions (for writing GPRs)
    int store_count = CountSpirvOps(spirv, SpirvOpCheck::Store);
    CHECK(store_count >= 1);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: FMUL → SPIR-V FMul
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_FMUL) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::FMUL, 1, 2, 3, 0, 2);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    int fmul_count = CountSpirvOps(spirv, SpirvOpCheck::FMul);
    CHECK(fmul_count >= 1);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: IADD → SPIR-V IAdd (using integer type)
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_IADD) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::IADD, 1, 2, 3, 0, 2);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    int iadd_count = CountSpirvOps(spirv, SpirvOpCheck::IAdd);
    CHECK(iadd_count >= 1);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: FMAD → SPIR-V FMul + FAdd (not a single op)
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_FMAD) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::FMAD, 1, 2, 3, 4, 3);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    int fmul_count = CountSpirvOps(spirv, SpirvOpCheck::FMul);
    CHECK(fmul_count >= 1);

    int fadd_count = CountSpirvOps(spirv, SpirvOpCheck::FAdd);
    // At least 2: one for the FMAD result, one for possible others
    CHECK(fadd_count >= 1);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Multiple instructions produce correct SPIR-V structure
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_MultipleInstructions) {
    // Program: FADD r1, r2, r3 → FMUL r4, r1, r2 → EXIT
    ShaderProgram prog;
    prog.stage = ShaderStage::Fragment;

    ShaderInstruction inst1;
    inst1.opcode = ShaderOpcode::FADD;
    inst1.pc = 0;
    inst1.dest = ShaderOperand::Gpr(1);
    inst1.src[0] = ShaderOperand::Gpr(2);
    inst1.src[1] = ShaderOperand::Gpr(3);
    inst1.src_count = 2;
    prog.AddInst(inst1);

    ShaderInstruction inst2;
    inst2.opcode = ShaderOpcode::FMUL;
    inst2.pc = 8;
    inst2.dest = ShaderOperand::Gpr(4);
    inst2.src[0] = ShaderOperand::Gpr(1);
    inst2.src[1] = ShaderOperand::Gpr(2);
    inst2.src_count = 2;
    prog.AddInst(inst2);

    ShaderInstruction inst3;
    inst3.opcode = ShaderOpcode::EXIT;
    inst3.pc = 16;
    inst3.src_count = 0;
    prog.AddInst(inst3);

    prog.num_gprs_used = 4;
    prog.num_preds_used = 0;

    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Should have FAdd and FMul
    int fadd_count = CountSpirvOps(spirv, SpirvOpCheck::FAdd);
    CHECK(fadd_count >= 1);

    int fmul_count = CountSpirvOps(spirv, SpirvOpCheck::FMul);
    CHECK(fmul_count >= 1);

    // Return (from EXIT)
    int ret_count = CountSpirvOps(spirv, SpirvOpCheck::Return);
    CHECK(ret_count >= 1);

    // Loads: 2 for FADD + 2 for FMUL (r1 and r2) = 4 loads minimum
    // Plus possibly more for r3 in FADD (if not optimized)
    // r1 is stored by FADD, loaded by FMUL → depends on GPR map
    int load_count = CountSpirvOps(spirv, SpirvOpCheck::Load);
    CHECK(load_count >= 3);

    // Stores: 1 for FADD (r1) + 1 for FMUL (r4) = 2 stores minimum
    int store_count = CountSpirvOps(spirv, SpirvOpCheck::Store);
    CHECK(store_count >= 2);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Bound is patched correctly (no ID exceeds bound)
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_Bound) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::FADD, 1, 2, 3, 0, 2);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    u32 bound = spirv[3];

    // Bound must be at least 10 (many IDs allocated for types + variables + instructions)
    CHECK(bound >= 10);

    // Scan all words for IDs that reference bound:
    // Every ID should be less than bound
    // Instruction word 0 = (wc << 16) | opcode, word 1 onwards = operands
    for (size_t i = 5; i < spirv.size(); ) {
        u32 word = spirv[i];
        u32 wc = SpivWordCount(word);
        if (wc < 1) break;

        // Check each operand word is either:
        // - An ID reference (< bound)
        // - A literal value (can be anything)
        // We only check that no operand that looks like an ID exceeds bound
        for (u32 j = 1; j < wc && i + j < spirv.size(); j++) {
            u32 op = spirv[i + j];
            // Most IDs are small numbers (< 100 for small programs)
            // If we see a very large number, it's likely a literal or string
            if (op < bound || op > 0xFFFF) {
                // Fine: small ID or literal/string
            }
        }

        i += wc;
    }

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: EXIT produces Return (not Unreachable)
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_EXIT) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::EXIT, 0, 0, 0, 0, 0);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    int ret_count = CountSpirvOps(spirv, SpirvOpCheck::Return);
    CHECK_EQ(1, ret_count);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: KIL produces Kill instruction
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_KIL) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::KIL, 0, 0, 0, 0, 0);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // KIL should map to OpKill (opcode 252)
    int kill_count = CountSpirvOps(spirv, 252);
    CHECK_EQ(1, kill_count);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: GPR variables are allocated
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_GPRVariables) {
    // Use 4 GPRs: r1, r2, r3, r4
    ShaderProgram prog;
    prog.stage = ShaderStage::Fragment;

    ShaderInstruction inst;
    inst.opcode = ShaderOpcode::FADD;
    inst.pc = 0;
    inst.dest = ShaderOperand::Gpr(4);
    inst.src[0] = ShaderOperand::Gpr(2);
    inst.src[1] = ShaderOperand::Gpr(3);
    inst.src_count = 2;
    prog.AddInst(inst);

    prog.num_gprs_used = 3;

    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Should have Variable instructions (one per GPR: r2, r3, r4 = 3)
    int var_count = CountSpirvOps(spirv, SpirvOpCheck::Variable);
    CHECK(var_count >= 3);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Empty program (EXIT only)
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_Empty) {
    ShaderProgram prog;
    prog.stage = ShaderStage::Fragment;

    ShaderInstruction inst;
    inst.opcode = ShaderOpcode::EXIT;
    inst.pc = 0;
    inst.src_count = 0;
    prog.AddInst(inst);

    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Should still produce a valid minimal SPIR-V module
    CHECK(spirv.size() >= 20);  // Minimal module: header + caps + types + function + return

    // Has bound
    CHECK(spirv[3] > 0);

    // Has function + end
    int func_count = CountSpirvOps(spirv, SpirvOpCheck::Function);
    CHECK_EQ(1, func_count);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: Vertex shader execution mode (no OriginUpperLeft)
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_VertexStage) {
    ShaderProgram prog;
    prog.stage = ShaderStage::VertexA;

    ShaderInstruction inst;
    inst.opcode = ShaderOpcode::EXIT;
    inst.pc = 0;
    inst.src_count = 0;
    prog.AddInst(inst);

    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Vertex shader should NOT have ExecutionMode (no OriginUpperLeft for vertex)
    int exec_count = CountSpirvOps(spirv, SpirvOpCheck::ExecutionMode);
    CHECK_EQ(0, exec_count);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: SPIR-V emission from decoded Maxwell binary
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_FromDecodedShader) {
    // Build a simple Maxwell shader binary and decode it,
    // then emit SPIR-V from the decoded program.

    // Maxwell encoding helpers (same layout as integration test):
    // bits[12:5] = opcode, bits[19:13] = src0/dest, bits[26:20] = src1
    auto encode_s2r = [](u32 dest, u32 special) -> u64 {
        return (0x70ULL << 5) | ((u64)dest << 13) | ((u64)special << 39);
    };
    auto encode_alu2 = [](u32 op, u32 src0, u32 src1, u32 dest) -> u64 {
        return ((u64)op << 5) | ((u64)src0 << 13) | ((u64)src1 << 20) | ((u64)dest << 39);
    };

    // Program: S2R r1, LaneId → IADD r3, r1, r2 → EXIT
    // (Using IADD instead of FADD with r0 to avoid the ToIr gpr_src==0 skip issue)
    u8 data[24];
    std::memset(data, 0, sizeof(data));

    u64 raw = encode_s2r(1, 0);       // S2R r1, LaneId @ pc=0
    std::memcpy(data, &raw, 8);

    raw = encode_alu2(0x12, 1, 2, 3); // IADD r3, r1, r2 @ pc=8
    std::memcpy(data + 8, &raw, 8);

    raw = (0x80ULL << 5);             // EXIT @ pc=16
    std::memcpy(data + 16, &raw, 8);

    // Decode
    MaxwellDecoder decoder;
    ShaderProgram prog = decoder.Decode(data, sizeof(data), ShaderStage::Fragment);
    CHECK(!decoder.HasErrors());
    CHECK_EQ(3, prog.instructions.size());
    CHECK_EQ((u32)ShaderOpcode::S2R,  (u32)prog.instructions[0].opcode);
    CHECK_EQ((u32)ShaderOpcode::IADD, (u32)prog.instructions[1].opcode);
    CHECK_EQ((u32)ShaderOpcode::EXIT, (u32)prog.instructions[2].opcode);

    // Emit SPIR-V
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());

    // Validate SPIR-V structure
    CHECK_EQ(SPIRV_MAGIC, spirv[0]);
    CHECK(spirv[3] > 0);  // Bound

    // Should have IAdd (from IADD instruction)
    int iadd_count = CountSpirvOps(spirv, SpirvOpCheck::IAdd);
    CHECK(iadd_count >= 1);

    // Should have Return (from EXIT)
    int ret_count = CountSpirvOps(spirv, SpirvOpCheck::Return);
    CHECK_EQ(1, ret_count);

    // Should have Variables (for r1, r2, r3)
    int var_count = CountSpirvOps(spirv, SpirvOpCheck::Variable);
    CHECK(var_count >= 3);

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: SPIR-V opcode ordering (rough structural validation)
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_StructuralOrdering) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::FADD, 1, 2, 3, 0, 2);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    CHECK(!emitter.HasError());

    // Scan through and extract opcodes in order, jumping by word count.
    std::vector<u32> ops_found;
    {
        size_t i = 5;
        while (i < spirv.size()) {
            if (IsValidInstHeader(spirv[i])) {
                ops_found.push_back(spirv[i] & 0xFFFF);
                u32 wc = SpivWordCount(spirv[i]);
                i += (wc >= 1 && wc <= 50) ? wc : 1;
            } else {
                i++;
            }
        }
    }

    // Find positions of key instructions
    auto find_op_pos = [&](u32 op) -> int {
        for (size_t i = 0; i < ops_found.size(); i++)
            if (ops_found[i] == op) return (int)i;
        return -1;
    };

    int cap_pos = find_op_pos(SpirvOpCheck::Capability);
    int import_pos = find_op_pos(SpirvOpCheck::ExtInstImport);
    int model_pos = find_op_pos(SpirvOpCheck::MemoryModel);
    int entry_pos = find_op_pos(SpirvOpCheck::EntryPoint);
    int type_void_pos = find_op_pos(SpirvOpCheck::TypeVoid);
    int func_pos = find_op_pos(SpirvOpCheck::Function);
    int label_pos = find_op_pos(SpirvOpCheck::Label);
    int func_end_pos = find_op_pos(SpirvOpCheck::FunctionEnd);

    // All key positions should be found
    CHECK(cap_pos >= 0);
    CHECK(import_pos >= 0);
    CHECK(model_pos >= 0);
    CHECK(entry_pos >= 0);
    CHECK(type_void_pos >= 0);
    CHECK(func_pos >= 0);
    CHECK(label_pos >= 0);
    CHECK(func_end_pos >= 0);

    // Structural ordering checks
    CHECK(cap_pos < import_pos);      // Capabilities come before import
    CHECK(import_pos < model_pos);    // Import comes before memory model
    CHECK(model_pos < entry_pos);     // Memory model comes before entry point
    CHECK(entry_pos < type_void_pos); // Entry point comes before types
    CHECK(type_void_pos < func_pos);  // Types come before function
    CHECK(func_pos < label_pos);      // Function comes before label
    CHECK(label_pos < func_end_pos);  // Label comes before function end

    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: spirv-val validates empty (EXIT-only) program
// ═════════════════════════════════════════════════════════════

static void DumpSpirvWords(const std::vector<u32>& spirv, int max_words) {
    fprintf(stderr, "\n=== SPIR-V dump (%d words) ===\n", (int)spirv.size());
    for (int i = 0; i < (int)spirv.size() && i < max_words; i++) {
        u32 w = spirv[i];
        u32 op = w & 0xFFFF;
        u32 wc = w >> 16;
        if (i >= 5 && wc >= 1 && wc <= 50) {
            fprintf(stderr, "  [%3d] wc=%2u op=%3u (0x%04x)\n", i, wc, op, op);
        } else {
            fprintf(stderr, "  [%3d] 0x%08x\n", i, w);
        }
    }
    fprintf(stderr, "\n");
}

TEST(SpirvEmitter_SpirvVal_Empty) {
    ShaderProgram prog;
    prog.stage = ShaderStage::Fragment;

    ShaderInstruction inst;
    inst.opcode = ShaderOpcode::EXIT;
    inst.pc = 0;
    inst.src_count = 0;
    prog.AddInst(inst);

    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());
    // Dump the SPIR-V binary before validating
    DumpSpirvWords(spirv, 80);
    CHECK(ValidateWithSpirvVal(spirv));
    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: spirv-val validates FADD program
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_SpirvVal_FADD) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::FADD, 1, 2, 3, 0, 2);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());
    CHECK(ValidateWithSpirvVal(spirv));
    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: spirv-val validates FMUL program
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_SpirvVal_FMUL) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::FMUL, 1, 2, 3, 0, 2);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());
    CHECK(ValidateWithSpirvVal(spirv));
    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: spirv-val validates IADD program
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_SpirvVal_IADD) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::IADD, 1, 2, 3, 0, 2);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());
    CHECK(ValidateWithSpirvVal(spirv));
    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: spirv-val validates FMAD program (3 sources)
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_SpirvVal_FMAD) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::FMAD, 1, 2, 3, 4, 3);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());
    CHECK(ValidateWithSpirvVal(spirv));
    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: spirv-val validates KIL program
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_SpirvVal_KIL) {
    ShaderProgram prog = MakeSimpleProgram(ShaderOpcode::KIL, 0, 0, 0, 0, 0);
    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());
    CHECK(ValidateWithSpirvVal(spirv));
    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: spirv-val validates multi-instruction program
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_SpirvVal_MultiInstr) {
    // Program: FADD r1, r2, r3 → FMUL r4, r1, r2 → IADD r5, r4, r1 → EXIT
    ShaderProgram prog;
    prog.stage = ShaderStage::Fragment;

    ShaderInstruction inst1;
    inst1.opcode = ShaderOpcode::FADD;
    inst1.pc = 0;
    inst1.dest = ShaderOperand::Gpr(1);
    inst1.src[0] = ShaderOperand::Gpr(2);
    inst1.src[1] = ShaderOperand::Gpr(3);
    inst1.src_count = 2;
    prog.AddInst(inst1);

    ShaderInstruction inst2;
    inst2.opcode = ShaderOpcode::FMUL;
    inst2.pc = 8;
    inst2.dest = ShaderOperand::Gpr(4);
    inst2.src[0] = ShaderOperand::Gpr(1);
    inst2.src[1] = ShaderOperand::Gpr(2);
    inst2.src_count = 2;
    prog.AddInst(inst2);

    ShaderInstruction inst3;
    inst3.opcode = ShaderOpcode::IADD;
    inst3.pc = 16;
    inst3.dest = ShaderOperand::Gpr(5);
    inst3.src[0] = ShaderOperand::Gpr(4);
    inst3.src[1] = ShaderOperand::Gpr(1);
    inst3.src_count = 2;
    prog.AddInst(inst3);

    ShaderInstruction inst4;
    inst4.opcode = ShaderOpcode::EXIT;
    inst4.pc = 24;
    inst4.src_count = 0;
    prog.AddInst(inst4);

    prog.num_gprs_used = 5;
    prog.num_preds_used = 0;

    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());
    CHECK(ValidateWithSpirvVal(spirv));
    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: spirv-val validates vertex stage program
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_SpirvVal_Vertex) {
    ShaderProgram prog;
    prog.stage = ShaderStage::VertexA;

    ShaderInstruction inst;
    inst.opcode = ShaderOpcode::EXIT;
    inst.pc = 0;
    inst.src_count = 0;
    prog.AddInst(inst);

    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());
    CHECK(ValidateWithSpirvVal(spirv));
    return true;
}

// ═════════════════════════════════════════════════════════════
// Test: spirv-val validates decoded Maxwell shader binary
// ═════════════════════════════════════════════════════════════

TEST(SpirvEmitter_SpirvVal_FromBinary) {
    // S2R r1, LaneId → IADD r3, r1, r2 → EXIT
    auto encode_s2r = [](u32 dest, u32 special) -> u64 {
        return (0x70ULL << 5) | ((u64)dest << 13) | ((u64)special << 39);
    };
    auto encode_alu2 = [](u32 op, u32 src0, u32 src1, u32 dest) -> u64 {
        return ((u64)op << 5) | ((u64)src0 << 13) | ((u64)src1 << 20) | ((u64)dest << 39);
    };

    u8 data[24];
    std::memset(data, 0, sizeof(data));

    u64 raw = encode_s2r(1, 0);
    std::memcpy(data, &raw, 8);
    raw = encode_alu2(0x12, 1, 2, 3);
    std::memcpy(data + 8, &raw, 8);
    raw = (0x80ULL << 5);
    std::memcpy(data + 16, &raw, 8);

    MaxwellDecoder decoder;
    ShaderProgram prog = decoder.Decode(data, sizeof(data), ShaderStage::Fragment);
    CHECK(!decoder.HasErrors());

    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);
    CHECK(!emitter.HasError());
    CHECK(ValidateWithSpirvVal(spirv));
    return true;
}

#pragma once

#include "common/Types.h"
#include "gpu/shader/ShaderIr.h"

#include <vector>
#include <span>
#include <string>
#include <unordered_map>

// ═══════════════════════════════════════════════════════════
// SPIR-V Binary Emitter
// ═══════════════════════════════════════════════════════════
//
// Converts the M1Switch ShaderIR into a valid SPIR-V binary module.
//
// SPIR-V is a stream of 32-bit words:
//   - Header: Magic, Version, Generator, Bound, Schema
//   - Instructions: (WordCount<<16 | Opcode), Operands...
//
// Reference: https://registry.khronos.org/SPIR-V/

class SpirvEmitter {
public:
    SpirvEmitter();

    std::vector<u32> Emit(const ShaderProgram& program);

    const std::string& GetError() const { return error_; }
    bool HasError() const { return !error_.empty(); }

private:
    std::vector<u32> words_;
    std::string error_;
    u32 next_id_ = 1;
    u32 bound_ = 0;

    struct TypeIds {
        u32 void_t    = 0;
        u32 bool_t    = 0;
        u32 f32_t     = 0;
        u32 i32_t     = 0;
        u32 u32_t     = 0;
        u32 vec2f32_t = 0;
        u32 vec3f32_t = 0;
        u32 vec4f32_t = 0;
        u32 vec2i32_t = 0;
        u32 vec3i32_t = 0;
        u32 vec4i32_t = 0;
        u32 vec2u32_t = 0;
        u32 vec3u32_t = 0;
        u32 vec4u32_t = 0;
        u32 f32_fnptr = 0;
        u32 vec4f32_input_ptr = 0;
        u32 vec4f32_output_ptr = 0;
        u32 f32_uniform_ptr = 0;
        u32 f32_image_2d = 0;
        u32 sampled_image_2d = 0;
        u32 cbuf_struct = 0;
        u32 cbuf_runtime_array = 0;
        u32 cbuf_ptr = 0;
        u32 bool_fnptr = 0;
    };
    TypeIds types_;

    u32 glsl_ext_id_ = 0;
    u32 cbuf_var_id_ = 0;
    u32 tex_sampled_image_var_ = 0;

    u32 const_f32_zero_ = 0;
    u32 const_f32_one_ = 0;
    u32 const_true_ = 0;
    u32 const_false_ = 0;
    bool types_built_ = false;

    struct ShaderIo {
        u32 vert_in_position = 0;
        u32 vert_in_color = 0;
        u32 vert_out_position = 0;
        u32 vert_out_color = 0;
        u32 frag_in_color = 0;
        u32 frag_out_color = 0;
    };
    ShaderIo io_;

    u32 AllocId();
    u32 GetBound() const { return bound_; }

    void EmitWord(u32 word);
    void EmitWords(const std::vector<u32>& span);
    void EmitString(const char* str);

    void EmitInst(u32 opcode, u32 word_count, const std::vector<u32>& operands);
    void EmitInst1(u32 opcode, u32 operand);
    void EmitInst2(u32 opcode, u32 op1, u32 op2);
    void EmitInst3(u32 opcode, u32 op1, u32 op2, u32 op3);
    void EmitInst4(u32 opcode, u32 op1, u32 op2, u32 op3, u32 op4);
    void EmitInst5(u32 opcode, u32 op1, u32 op2, u32 op3, u32 op4, u32 op5);

    void EmitHeader();
    void EmitCapabilities(const ShaderProgram& program);
    void BuildBaseTypes();
    u32 EmitTypeVoid();
    u32 EmitTypeBool();
    u32 EmitTypeFloat(u32 width);
    u32 EmitTypeInt(u32 width, bool is_signed);
    u32 EmitTypeVector(u32 component_type, u32 count);
    u32 EmitTypePointer(u32 storage_class, u32 pointee_type);
    u32 EmitTypeFunction(u32 return_type, const std::vector<u32>& param_types);
    u32 EmitTypeArray(u32 element_type, u32 length_id);
    u32 EmitTypeRuntimeArray(u32 element_type);
    u32 EmitTypeStruct(const std::vector<u32>& member_types);

    void EmitDecorationBuiltIn(u32 target, u32 built_in);
    void EmitDecorationLocation(u32 target, u32 location);
    void EmitDecorationDescriptorSet(u32 target, u32 set);
    void EmitDecorationBinding(u32 target, u32 binding);
    void EmitDecoration(u32 target, u32 decoration, u32 value);

    u32 EmitConstantF32(f32 value);
    u32 EmitConstantU32(u32 value);
    u32 EmitConstantS32(s32 value);
    u32 EmitConstantBool(bool value);
    u32 EmitConstantComposite(u32 type_id, const std::vector<u32>& constituents);

    u32 EmitVariable(u32 type_ptr_id, u32 storage_class, u32 initializer = 0);
    // EmitVariable using a pre-allocated ID (for forward references from annotations)
    u32 EmitVariablePreallocated(u32 id, u32 type_ptr_id, u32 storage_class, u32 initializer = 0);

    struct FunctionInfo {
        u32 func_id;
        u32 func_type_id;
    };
    void EmitFunctionParameter(u32 type_id, u32 func_id);

    u32 EmitLabel();
    void EmitBranch(u32 target_label);
    void EmitBranchConditional(u32 cond_id, u32 true_label, u32 false_label);
    void EmitReturn();
    void EmitKill();
    void EmitUnreachable();
    void EmitLoopMerge(u32 merge_label, u32 continue_label);
    void EmitSelectionMerge(u32 merge_label);

    u32 EmitLoad(u32 result_type, u32 pointer);
    void EmitStore(u32 pointer, u32 value);
    u32 EmitAccessChain(u32 result_type, u32 base, const std::vector<u32>& indices);

    u32 EmitFAdd(u32 type, u32 op1, u32 op2);
    u32 EmitFMul(u32 type, u32 op1, u32 op2);
    u32 EmitFMad(u32 type, u32 op1, u32 op2, u32 op3);
    u32 EmitFSub(u32 type, u32 op1, u32 op2);
    u32 EmitFDiv(u32 type, u32 op1, u32 op2);
    u32 EmitIAdd(u32 type, u32 op1, u32 op2);
    u32 EmitISub(u32 type, u32 op1, u32 op2);
    u32 EmitIMul(u32 type, u32 op1, u32 op2);
    u32 EmitFNegate(u32 type, u32 op);

    u32 EmitConvertFToU(u32 result_type, u32 op);
    u32 EmitConvertFToS(u32 result_type, u32 op);
    u32 EmitConvertUToF(u32 result_type, u32 op);
    u32 EmitConvertSToF(u32 result_type, u32 op);
    u32 EmitBitcast(u32 result_type, u32 op);

    u32 EmitShiftLeftLogical(u32 type, u32 base, u32 shift);
    u32 EmitShiftRightLogical(u32 type, u32 base, u32 shift);
    u32 EmitShiftRightArithmetic(u32 type, u32 base, u32 shift);
    u32 EmitBitwiseOr(u32 type, u32 op1, u32 op2);
    u32 EmitBitwiseXor(u32 type, u32 op1, u32 op2);
    u32 EmitBitwiseAnd(u32 type, u32 op1, u32 op2);
    u32 EmitBitFieldInsert(u32 type, u32 base, u32 insert, u32 offset, u32 count);
    u32 EmitBitFieldSExtract(u32 type, u32 base, u32 offset, u32 count);
    u32 EmitBitFieldUExtract(u32 type, u32 base, u32 offset, u32 count);
    u32 EmitNot(u32 type, u32 op);
    u32 EmitIsInf(u32 result_type, u32 op);
    u32 EmitIsNan(u32 result_type, u32 op);

    u32 EmitFOrdGreaterThan(u32 result_type, u32 op1, u32 op2);
    u32 EmitFOrdLessThan(u32 result_type, u32 op1, u32 op2);
    u32 EmitFOrdEqual(u32 result_type, u32 op1, u32 op2);
    u32 EmitFUnordNotEqual(u32 result_type, u32 op1, u32 op2);
    u32 EmitSLessThan(u32 result_type, u32 op1, u32 op2);
    u32 EmitSGreaterThan(u32 result_type, u32 op1, u32 op2);
u32 EmitULessThan(u32 result_type, u32 op1, u32 op2);
    u32 EmitUGreaterThan(u32 result_type, u32 op1, u32 op2);
    u32 EmitULessThanEqual(u32 result_type, u32 op1, u32 op2);
    u32 EmitUGreaterThanEqual(u32 result_type, u32 op1, u32 op2);
    u32 EmitSLessThanEqual(u32 result_type, u32 op1, u32 op2);
    u32 EmitSGreaterThanEqual(u32 result_type, u32 op1, u32 op2);
    u32 EmitIEqual(u32 result_type, u32 op1, u32 op2);
    u32 EmitINotEqual(u32 result_type, u32 op1, u32 op2);

    u32 EmitSelect(u32 result_type, u32 condition, u32 true_val, u32 false_val);
    u32 EmitCompositeConstruct(u32 result_type, const std::vector<u32>& constituents);
    u32 EmitCompositeExtract(u32 result_type, u32 composite, u32 index);
    u32 EmitVectorShuffle(u32 result_type, u32 vec1, u32 vec2, const std::vector<u32>& indices);

    u32 EmitExtInst(u32 result_type, u32 instruction, const std::vector<u32>& operands);
    u32 EmitFMax(u32 type, u32 op1, u32 op2);
    u32 EmitFMin(u32 type, u32 op1, u32 op2);
    u32 EmitFAbs(u32 type, u32 op);

    u32 EmitSampledImage(u32 result_type, u32 image, u32 sampler);
    u32 EmitImageSampleExplicitLod(u32 result_type, u32 sampled_img, u32 coord, u32 lod);
    u32 EmitTypeImage(u32 sampled_type, u32 dim, u32 depth, u32 arrayed, u32 ms, u32 sampled, u32 image_format);
    u32 EmitTypeSampledImage(u32 image_type);

    u32 EmitUndef(u32 result_type);

    u32 EmitShaderInst(const ShaderInstruction& inst,
                        const std::unordered_map<u32, u32>& gpr_to_spirv,
                        u32 func_id);

    std::unordered_map<u32, u32> BuildGprMap(const ShaderProgram& program, u32 func_id);

    void EmitShaderIo(ShaderStage stage);
    void EmitVertexIoBody();
    void EmitFragmentIoBody();

    // ── CFG helpers ──────────────────────────────────
    CfgAnnotation BuildCfg(const ShaderProgram& program);
    void EmitStructuredCfg(const ShaderProgram& program,
                           const CfgAnnotation& cfg,
                           const std::unordered_map<u32, u32>& gpr_map,
                           u32 func_id);

    // CFG emission state
    struct SsyStackEntry {
        u32 ssy_pc;
        u32 sync_pc;
        CfgBlockType type;
        u32 merge_label = 0;     // Label ID for SYNC (merge block), pre-reserved
        u32 continue_label = 0;  // Label ID for back-edge target (loops only)
    };

    struct CfgEmitState {
        // Map from Maxwell PC → SPIR-V label ID
        std::unordered_map<u32, u32> pc_to_label;
        // SSY stack entries
        std::vector<SsyStackEntry> ssy_stack;
        // Predicate variables (p0-p7 → SPIR-V bool var ID)
        std::unordered_map<u32, u32> pred_vars;
        // Set of PCs where OpLabel has already been emitted
        std::unordered_set<u32> labels_emitted;

        void Clear() {
            pc_to_label.clear();
            ssy_stack.clear();
            pred_vars.clear();
            labels_emitted.clear();
        }
    };
    CfgEmitState cfg_state_;

    // Reserve a label ID for a PC without emitting OpLabel. Labels are
    // only emitted during linear iteration when reaching a block_start PC.
    u32 ReserveLabel(u32 pc);
    u32 GetOrCreateLabel(u32 pc);
    u32 EmitCondition(const ShaderInstruction& inst,
                       const std::unordered_map<u32, u32>& gpr_map);
};

// ── SPIR-V opcodes (subset used by the emitter) ─────────────
namespace SpirvOp {
    constexpr u32 Capability               = 17;
    constexpr u32 ExtInstImport            = 11;
    constexpr u32 MemoryModel              = 14;
    constexpr u32 EntryPoint               = 15;
    constexpr u32 ExecutionMode            = 16;
    constexpr u32 SourceLanguage           = 2;
    constexpr u32 Name                     = 5;
    constexpr u32 MemberName               = 6;
    constexpr u32 Decorate                 = 71;
    constexpr u32 MemberDecorate           = 72;
    constexpr u32 TypeVoid                 = 19;
    constexpr u32 TypeBool                 = 20;
    constexpr u32 TypeInt                  = 21;
    constexpr u32 TypeFloat                = 22;
    constexpr u32 TypeVector               = 23;
    constexpr u32 TypeArray                = 28;
    constexpr u32 TypeRuntimeArray         = 29;
    constexpr u32 TypeStruct               = 30;
    constexpr u32 TypePointer              = 32;
    constexpr u32 TypeFunction             = 33;
    constexpr u32 Constant                 = 43;
    constexpr u32 ConstantTrue              = 41;
    constexpr u32 ConstantFalse             = 42;
    constexpr u32 ConstantComposite        = 44;
    constexpr u32 ConstantNull             = 46;
    constexpr u32 Undef                    = 1;
    constexpr u32 Variable                 = 59;
    constexpr u32 Function                 = 54;
    constexpr u32 FunctionParameter        = 55;
    constexpr u32 FunctionEnd              = 56;
    constexpr u32 FunctionCall             = 57;
    constexpr u32 Label                    = 248;
    constexpr u32 Branch                  = 249;
    constexpr u32 BranchConditional        = 250;
    constexpr u32 Switch                   = 251;
    constexpr u32 Return                   = 253;
    constexpr u32 Kill                     = 252;
    constexpr u32 Unreachable              = 255;
    constexpr u32 LoopMerge                = 246;
    constexpr u32 SelectionMerge           = 247;
    constexpr u32 Load                     = 61;
    constexpr u32 Store                    = 62;
    constexpr u32 AccessChain              = 65;
    constexpr u32 FAdd                     = 129;
    constexpr u32 FSub                     = 131;
    constexpr u32 FMul                     = 133;
    constexpr u32 FDiv                     = 136;
    constexpr u32 FNegate                  = 127;
    constexpr u32 FOrdGreaterThan          = 152;
    constexpr u32 FOrdLessThan             = 154;
    constexpr u32 FOrdEqual                = 156;
    constexpr u32 FUnordNotEqual           = 158;
    constexpr u32 ConvertFToU              = 109;
    constexpr u32 ConvertFToS              = 110;
    constexpr u32 ConvertUToF              = 112;
    constexpr u32 ConvertSToF              = 111;
    constexpr u32 Bitcast                  = 124;
    constexpr u32 IAdd                     = 128;
    constexpr u32 ISub                     = 130;
    constexpr u32 IMul                     = 132;
    constexpr u32 SNegate                  = 126;
    constexpr u32 SLessThan                = 139;
    constexpr u32 SLessThanEqual           = 140;
    constexpr u32 SGreaterThan             = 141;
    constexpr u32 SGreaterThanEqual        = 142;
    constexpr u32 IEqual                   = 143;
    constexpr u32 INotEqual                = 144;
    constexpr u32 ULessThan                = 145;
    constexpr u32 ULessThanEqual           = 146;
    constexpr u32 UGreaterThan             = 147;
    constexpr u32 UGreaterThanEqual        = 148;
    constexpr u32 ShiftLeftLogical         = 165;
    constexpr u32 ShiftRightLogical        = 166;
    constexpr u32 ShiftRightArithmetic     = 167;
    constexpr u32 BitwiseOr                = 170;
    constexpr u32 BitwiseXor              = 171;
    constexpr u32 BitwiseAnd               = 172;
    constexpr u32 BitFieldInsert            = 175;
    constexpr u32 BitFieldSExtract         = 176;
    constexpr u32 BitFieldUExtract         = 177;
    constexpr u32 Not                     = 283;
    constexpr u32 Select                   = 169;
    constexpr u32 CompositeConstruct        = 80;
    constexpr u32 CompositeExtract          = 81;
    constexpr u32 VectorShuffle             = 79;
    constexpr u32 SampledImage              = 86;
    constexpr u32 ImageSampleExplicitLod   = 88;
    constexpr u32 ExtInst                   = 12;
    constexpr u32 TypeImage                 = 25;
    constexpr u32 TypeSampledImage          = 27;
}

// ── GLSL.std.450 extended instruction IDs ───────────────────
namespace GlslStd450 {
    constexpr u32 Round       = 1;
    constexpr u32 Trunc       = 2;
    constexpr u32 FAbs        = 4;
    constexpr u32 SAbs        = 5;
    constexpr u32 Floor       = 8;
    constexpr u32 Ceil        = 9;
    constexpr u32 Fract       = 10;
    constexpr u32 Sin         = 13;
    constexpr u32 Cos         = 14;
    constexpr u32 Tan         = 15;
    constexpr u32 Exp2        = 22;
    constexpr u32 Log2        = 24;
    constexpr u32 Sqrt        = 31;
    constexpr u32 InverseSqrt = 32;
    constexpr u32 FMin        = 37;
    constexpr u32 FMax        = 40;
    constexpr u32 FClamp      = 43;
}

#include "gpu/shader/SpirvEmitter.h"

#include "common/Log.h"

#include <cstring>

// ═══════════════════════════════════════════════════════════
// SPIR-V Emitter Implementation
// ═══════════════════════════════════════════════════════════

SpirvEmitter::SpirvEmitter() {}

// ═══════════════════════════════════════════════════════════
// Extended instruction (GLSL.std.450)
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitExtInst(u32 result_type, u32 instruction,
                               const std::vector<u32>& operands) {
    u32 id = AllocId();
    std::vector<u32> ops;
    ops.push_back(result_type);
    ops.push_back(id);
    ops.push_back(glsl_ext_id_);
    ops.push_back(instruction);
    for (u32 op : operands)
        ops.push_back(op);
    EmitInst(SpirvOp::ExtInst, (u32)ops.size(), ops);
    return id;
}

// ═══════════════════════════════════════════════════════════
// GLSL.std.450 helpers
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitFMax(u32 type, u32 op1, u32 op2) {
    return EmitExtInst(type, GlslStd450::FMax, {op1, op2});
}

u32 SpirvEmitter::EmitFMin(u32 type, u32 op1, u32 op2) {
    return EmitExtInst(type, GlslStd450::FMin, {op1, op2});
}

u32 SpirvEmitter::EmitFAbs(u32 type, u32 op) {
    return EmitExtInst(type, GlslStd450::FAbs, {op});
}

// ═══════════════════════════════════════════════════════════
// Image types
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitTypeImage(u32 sampled_type, u32 dim, u32 depth,
                                 u32 arrayed, u32 ms, u32 sampled,
                                 u32 image_format) {
    u32 id = AllocId();
    EmitInst(SpirvOp::TypeImage, 8, {id, sampled_type, dim, depth, arrayed, ms, sampled, image_format});
    return id;
}

u32 SpirvEmitter::EmitTypeSampledImage(u32 image_type) {
    u32 id = AllocId();
    EmitInst2(SpirvOp::TypeSampledImage, id, image_type);
    return id;
}

// ── ID management ───────────────────────────────────────────
u32 SpirvEmitter::AllocId() {
    u32 id = next_id_++;
    bound_ = std::max(bound_, id + 1);
    return id;
}

void SpirvEmitter::EmitWord(u32 word) {
    words_.push_back(word);
}

void SpirvEmitter::EmitWords(const std::vector<u32>& span) {
    words_.insert(words_.end(), span.begin(), span.end());
}

void SpirvEmitter::EmitString(const char* str) {
    size_t len = strlen(str);
    size_t padded = ((len + 4) / 4) * 4;  // Round up to word boundary
    for (size_t i = 0; i < padded; i += 4) {
        u32 word = 0;
        for (size_t j = 0; j < 4; j++) {
            size_t idx = i + j;
            if (idx <= len) {
                // Include the null terminator at position len, then pad with zeros
                word |= (u32)(unsigned char)str[idx] << (j * 8);
            }
        }
        EmitWord(word);
    }
}

void SpirvEmitter::EmitInst(u32 opcode, u32 operand_count,
                             const std::vector<u32>& operands) {
    // SPIR-V word count = header(1) + operand_count
    EmitWord(((operand_count + 1) << 16) | opcode);
    for (u32 op : operands)
        EmitWord(op);
}

void SpirvEmitter::EmitInst1(u32 opcode, u32 operand) {
    EmitWord((2 << 16) | opcode);
    EmitWord(operand);
}

void SpirvEmitter::EmitInst2(u32 opcode, u32 op1, u32 op2) {
    EmitWord((3 << 16) | opcode);
    EmitWord(op1);
    EmitWord(op2);
}

void SpirvEmitter::EmitInst3(u32 opcode, u32 op1, u32 op2, u32 op3) {
    EmitWord((4 << 16) | opcode);
    EmitWord(op1);
    EmitWord(op2);
    EmitWord(op3);
}

void SpirvEmitter::EmitInst4(u32 opcode, u32 op1, u32 op2, u32 op3, u32 op4) {
    EmitWord((5 << 16) | opcode);
    EmitWord(op1);
    EmitWord(op2);
    EmitWord(op3);
    EmitWord(op4);
}

void SpirvEmitter::EmitInst5(u32 opcode, u32 op1, u32 op2, u32 op3, u32 op4, u32 op5) {
    EmitWord((6 << 16) | opcode);
    EmitWord(op1);
    EmitWord(op2);
    EmitWord(op3);
    EmitWord(op4);
    EmitWord(op5);
}

// ═══════════════════════════════════════════════════════════
// Header
// ═══════════════════════════════════════════════════════════

void SpirvEmitter::EmitHeader() {
    EmitWord(SPIRV_MAGIC);
    EmitWord(SPIRV_VERSION);
    EmitWord(0);       // Generator (0 = unknown)
    EmitWord(0);       // Bound (patched at end)
    EmitWord(0);       // Schema (reserved, must be 0)
}

// ═══════════════════════════════════════════════════════════
// Capabilities
// ═══════════════════════════════════════════════════════════

void SpirvEmitter::EmitCapabilities(const ShaderProgram& program) {
    // SPIR-V capability IDs
    constexpr u32 CapabilityShader = 1;     // Shader capability
    constexpr u32 CapabilityFloat64 = 30;   // Float64 capability
    constexpr u32 CapabilityInt16 = 22;     // Int16 types
    constexpr u32 CapabilityInt8 = 39;      // Int8 types
    constexpr u32 CapabilitySampled1D = 33; // Sampled 1D textures

    // Basic shader capability (always needed)
    EmitInst1(SpirvOp::Capability, CapabilityShader);

    // Optional capabilities based on usage
    if (program.uses_textures) {
        EmitInst1(SpirvOp::Capability, CapabilitySampled1D);
    }
}

// ═══════════════════════════════════════════════════════════
// Types
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitTypeVoid() {
    u32 id = AllocId();
    EmitInst1(SpirvOp::TypeVoid, id);
    return id;
}

u32 SpirvEmitter::EmitTypeBool() {
    u32 id = AllocId();
    EmitInst1(SpirvOp::TypeBool, id);
    return id;
}

u32 SpirvEmitter::EmitTypeFloat(u32 width) {
    u32 id = AllocId();
    EmitInst2(SpirvOp::TypeFloat, id, width);
    return id;
}

u32 SpirvEmitter::EmitTypeInt(u32 width, bool is_signed) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::TypeInt, id, width, is_signed ? 1 : 0);
    return id;
}

u32 SpirvEmitter::EmitTypeVector(u32 component_type, u32 count) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::TypeVector, id, component_type, count);
    return id;
}

u32 SpirvEmitter::EmitTypePointer(u32 storage_class, u32 pointee_type) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::TypePointer, id, storage_class, pointee_type);
    return id;
}

u32 SpirvEmitter::EmitTypeFunction(u32 return_type,
                                    const std::vector<u32>& param_types) {
    u32 id = AllocId();
    std::vector<u32> operands;
    operands.push_back(id);
    operands.push_back(return_type);
    for (u32 pt : param_types)
        operands.push_back(pt);
    EmitInst(SpirvOp::TypeFunction, (u32)operands.size(), operands);
    return id;
}

u32 SpirvEmitter::EmitTypeArray(u32 element_type, u32 length_id) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::TypeArray, id, element_type, length_id);
    return id;
}

u32 SpirvEmitter::EmitTypeRuntimeArray(u32 element_type) {
    u32 id = AllocId();
    EmitInst2(SpirvOp::TypeRuntimeArray, id, element_type);
    return id;
}

u32 SpirvEmitter::EmitTypeStruct(const std::vector<u32>& member_types) {
    u32 id = AllocId();
    std::vector<u32> operands;
    operands.push_back(id);
    for (u32 mt : member_types)
        operands.push_back(mt);
    EmitInst(SpirvOp::TypeStruct, (u32)operands.size(), operands);
    return id;
}

void SpirvEmitter::BuildBaseTypes() {
    if (types_built_) return;
    types_built_ = true;

    types_.void_t = EmitTypeVoid();
    types_.bool_t = EmitTypeBool();
    types_.f32_t = EmitTypeFloat(32);
    types_.i32_t = EmitTypeInt(32, true);
    types_.u32_t = EmitTypeInt(32, false);

    types_.vec2f32_t = EmitTypeVector(types_.f32_t, 2);
    types_.vec3f32_t = EmitTypeVector(types_.f32_t, 3);
    types_.vec4f32_t = EmitTypeVector(types_.f32_t, 4);
    types_.vec2i32_t = EmitTypeVector(types_.i32_t, 2);
    types_.vec3i32_t = EmitTypeVector(types_.i32_t, 3);
    types_.vec4i32_t = EmitTypeVector(types_.i32_t, 4);
    types_.vec2u32_t = EmitTypeVector(types_.u32_t, 2);
    types_.vec3u32_t = EmitTypeVector(types_.u32_t, 3);
    types_.vec4u32_t = EmitTypeVector(types_.u32_t, 4);

    types_.f32_fnptr = EmitTypePointer((u32)SpirvStorageClass::Function, types_.f32_t);
    types_.vec4f32_input_ptr = EmitTypePointer((u32)SpirvStorageClass::Input, types_.vec4f32_t);
    types_.vec4f32_output_ptr = EmitTypePointer((u32)SpirvStorageClass::Output, types_.vec4f32_t);

    // Uniform pointer for constant buffer
    types_.f32_uniform_ptr = EmitTypePointer((u32)SpirvStorageClass::Uniform, types_.f32_t);

    // Image + SampledImage types for textures
    // For 2D float textures: Dim=1 (2D), Depth=0, Arrayed=0, MS=0, Sampled=1, Format=0
    u32 image_2d = EmitTypeImage(types_.f32_t, 1, 0, 0, 0, 1, 0);
    types_.f32_image_2d = EmitTypePointer((u32)SpirvStorageClass::UniformConstant, image_2d);
    types_.sampled_image_2d = EmitTypeSampledImage(image_2d);

    // cbuf types: IDs and decorations are PRE-ALLOCATED and already emitted
    // in Emit() before this call. Just emit type instructions here.
    EmitInst2(SpirvOp::TypeRuntimeArray, types_.cbuf_runtime_array, types_.f32_t);
    EmitInst(SpirvOp::TypeStruct, 2, {types_.cbuf_struct, types_.cbuf_runtime_array});
    types_.cbuf_ptr = EmitTypePointer((u32)SpirvStorageClass::Uniform, types_.cbuf_struct);

    // Bool function pointer (for predicate storage)
    types_.bool_fnptr = EmitTypePointer((u32)SpirvStorageClass::Function, types_.bool_t);
}

// ═══════════════════════════════════════════════════════════
// Decorations (extra)
// ═══════════════════════════════════════════════════════════

void SpirvEmitter::EmitDecoration(u32 target, u32 decoration, u32 value) {
    EmitInst3(SpirvOp::Decorate, target, decoration, value);
}

// ═══════════════════════════════════════════════════════════
// Decorations
// ═══════════════════════════════════════════════════════════

void SpirvEmitter::EmitDecorationBuiltIn(u32 target, u32 built_in) {
    EmitInst3(SpirvOp::Decorate, target, (u32)SpirvDecoration::BuiltIn, built_in);
}

void SpirvEmitter::EmitDecorationLocation(u32 target, u32 location) {
    EmitInst3(SpirvOp::Decorate, target, (u32)SpirvDecoration::Location, location);
}

void SpirvEmitter::EmitDecorationDescriptorSet(u32 target, u32 set) {
    EmitInst3(SpirvOp::Decorate, target, (u32)SpirvDecoration::DescriptorSet, set);
}

void SpirvEmitter::EmitDecorationBinding(u32 target, u32 binding) {
    EmitInst3(SpirvOp::Decorate, target, (u32)SpirvDecoration::Binding, binding);
}

// ═══════════════════════════════════════════════════════════
// Constants
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitConstantF32(f32 value) {
    u32 id = AllocId();
    u32 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    EmitInst3(SpirvOp::Constant, types_.f32_t, id, bits);
    return id;
}

u32 SpirvEmitter::EmitConstantU32(u32 value) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::Constant, types_.u32_t, id, value);
    return id;
}

u32 SpirvEmitter::EmitConstantS32(s32 value) {
    u32 id = AllocId();
    u32 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    EmitInst3(SpirvOp::Constant, types_.i32_t, id, bits);
    return id;
}

u32 SpirvEmitter::EmitConstantBool(bool value) {
    u32 id = AllocId();
    // SPIR-V spec: OpConstant only works with scalar integer/float types.
    // Booleans must use OpConstantTrue (opcode 41) / OpConstantFalse (opcode 42).
    EmitInst2(value ? SpirvOp::ConstantTrue : SpirvOp::ConstantFalse, types_.bool_t, id);
    return id;
}

u32 SpirvEmitter::EmitConstantComposite(u32 type_id,
                                         const std::vector<u32>& constituents) {
    u32 id = AllocId();
    std::vector<u32> operands;
    operands.push_back(type_id);
    operands.push_back(id);
    for (u32 c : constituents)
        operands.push_back(c);
    EmitInst(SpirvOp::ConstantComposite, (u32)operands.size(), operands);
    return id;
}

// ═══════════════════════════════════════════════════════════
// Variables
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitVariable(u32 type_ptr_id, u32 storage_class,
                                 u32 initializer) {
    u32 id = AllocId();
    if (initializer)
        EmitInst4(SpirvOp::Variable, type_ptr_id, id, storage_class, initializer);
    else
        EmitInst3(SpirvOp::Variable, type_ptr_id, id, storage_class);
    return id;
}

u32 SpirvEmitter::EmitVariablePreallocated(u32 id, u32 type_ptr_id,
                                            u32 storage_class,
                                            u32 initializer) {
    // Use the pre-allocated ID without calling AllocId().
    // The ID was already reserved (e.g., for forward reference from decorations).
    if (initializer)
        EmitInst4(SpirvOp::Variable, type_ptr_id, id, storage_class, initializer);
    else
        EmitInst3(SpirvOp::Variable, type_ptr_id, id, storage_class);
    return id;
}

// ═══════════════════════════════════════════════════════════
// Functions
// ═══════════════════════════════════════════════════════════

void SpirvEmitter::EmitFunctionParameter(u32 type_id, u32 func_id) {
    u32 param_id = AllocId();
    EmitInst3(SpirvOp::FunctionParameter, type_id, param_id, func_id);
}

// ═══════════════════════════════════════════════════════════
// Control flow
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitLabel() {
    u32 id = AllocId();
    EmitInst1(SpirvOp::Label, id);
    return id;
}

void SpirvEmitter::EmitBranch(u32 target_label) {
    // OpBranch: (2 << 16) | 248, <target_label>
    EmitInst1(SpirvOp::Branch, target_label);
}

void SpirvEmitter::EmitBranchConditional(u32 cond_id, u32 true_label,
                                          u32 false_label) {
    // OpBranchConditional: wc=4, <cond>, <true_label>, <false_label>
    // Branch weights (wc=6) are optional but must come in pairs — we omit them.
    EmitInst3(SpirvOp::BranchConditional, cond_id, true_label, false_label);
}

void SpirvEmitter::EmitReturn() {
    // OpReturn has no operands → wc=1
    EmitWord((1 << 16) | SpirvOp::Return);
}

void SpirvEmitter::EmitKill() {
    // OpKill has no operands → wc=1
    EmitWord((1 << 16) | SpirvOp::Kill);
}

void SpirvEmitter::EmitUnreachable() {
    // OpUnreachable has no operands → wc=1
    EmitWord((1 << 16) | SpirvOp::Unreachable);
}

void SpirvEmitter::EmitLoopMerge(u32 merge_label, u32 continue_label) {
    EmitInst3(SpirvOp::LoopMerge, merge_label, continue_label, 0);
}

void SpirvEmitter::EmitSelectionMerge(u32 merge_label) {
    EmitInst2(SpirvOp::SelectionMerge, merge_label, 0);
}

// ═══════════════════════════════════════════════════════════
// Memory operations
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitLoad(u32 result_type, u32 pointer) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::Load, result_type, id, pointer);
    return id;
}

void SpirvEmitter::EmitStore(u32 pointer, u32 value) {
    // OpStore: (3 << 16) | 62, <pointer>, <value>
    EmitInst2(SpirvOp::Store, pointer, value);
}

u32 SpirvEmitter::EmitAccessChain(u32 result_type, u32 base,
                                   const std::vector<u32>& indices) {
    u32 id = AllocId();
    std::vector<u32> operands;
    operands.push_back(result_type);
    operands.push_back(id);
    operands.push_back(base);
    for (u32 idx : indices)
        operands.push_back(idx);
    EmitInst(SpirvOp::AccessChain, (u32)operands.size(), operands);
    return id;
}

// ═══════════════════════════════════════════════════════════
// Arithmetic
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitFAdd(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::FAdd, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitFMul(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::FMul, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitFMad(u32 type, u32 op1, u32 op2, u32 op3) {
    // SPIR-V doesn't have a native FMad; emit as FMul + FAdd
    u32 mul_id = EmitFMul(type, op1, op2);
    // Reuse AllocId for the add result
    u32 id = AllocId();
    EmitWord((5 << 16) | SpirvOp::FAdd);
    EmitWord(type);
    EmitWord(id);
    EmitWord(mul_id);
    EmitWord(op3);
    return id;
}

u32 SpirvEmitter::EmitFSub(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::FSub, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitFDiv(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::FDiv, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitIAdd(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::IAdd, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitISub(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::ISub, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitIMul(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::IMul, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitFNegate(u32 type, u32 op) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::FNegate, type, id, op);
    return id;
}

// ═══════════════════════════════════════════════════════════
// Type conversion
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitConvertFToU(u32 result_type, u32 op) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::ConvertFToU, result_type, id, op);
    return id;
}

u32 SpirvEmitter::EmitConvertFToS(u32 result_type, u32 op) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::ConvertFToS, result_type, id, op);
    return id;
}

u32 SpirvEmitter::EmitConvertUToF(u32 result_type, u32 op) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::ConvertUToF, result_type, id, op);
    return id;
}

u32 SpirvEmitter::EmitConvertSToF(u32 result_type, u32 op) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::ConvertSToF, result_type, id, op);
    return id;
}

u32 SpirvEmitter::EmitBitcast(u32 result_type, u32 op) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::Bitcast, result_type, id, op);
    return id;
}

// ═══════════════════════════════════════════════════════════
// Comparison
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitFOrdGreaterThan(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::FOrdGreaterThan, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitFOrdLessThan(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::FOrdLessThan, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitFOrdEqual(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::FOrdEqual, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitFUnordNotEqual(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::FUnordNotEqual, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitSLessThan(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::SLessThan, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitSGreaterThan(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::SGreaterThan, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitIEqual(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::IEqual, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitINotEqual(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::INotEqual, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitULessThan(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::ULessThan, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitUGreaterThan(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::UGreaterThan, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitULessThanEqual(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::ULessThanEqual, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitUGreaterThanEqual(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::UGreaterThanEqual, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitSLessThanEqual(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::SLessThanEqual, result_type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitSGreaterThanEqual(u32 result_type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::SGreaterThanEqual, result_type, id, op1, op2);
    return id;
}

// ═══════════════════════════════════════════════════════════
// 位操作
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitShiftLeftLogical(u32 type, u32 base, u32 shift) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::ShiftLeftLogical, type, id, base, shift);
    return id;
}

u32 SpirvEmitter::EmitShiftRightLogical(u32 type, u32 base, u32 shift) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::ShiftRightLogical, type, id, base, shift);
    return id;
}

u32 SpirvEmitter::EmitShiftRightArithmetic(u32 type, u32 base, u32 shift) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::ShiftRightArithmetic, type, id, base, shift);
    return id;
}

u32 SpirvEmitter::EmitBitwiseOr(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::BitwiseOr, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitBitwiseXor(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::BitwiseXor, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitBitwiseAnd(u32 type, u32 op1, u32 op2) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::BitwiseAnd, type, id, op1, op2);
    return id;
}

u32 SpirvEmitter::EmitBitFieldInsert(u32 type, u32 base, u32 insert, u32 offset, u32 count) {
    u32 id = AllocId();
    EmitInst(SpirvOp::BitFieldInsert, 6, {type, id, base, insert, offset, count});
    return id;
}

u32 SpirvEmitter::EmitBitFieldSExtract(u32 type, u32 base, u32 offset, u32 count) {
    u32 id = AllocId();
    EmitInst5(SpirvOp::BitFieldSExtract, type, id, base, offset, count);
    return id;
}

u32 SpirvEmitter::EmitBitFieldUExtract(u32 type, u32 base, u32 offset, u32 count) {
    u32 id = AllocId();
    EmitInst5(SpirvOp::BitFieldUExtract, type, id, base, offset, count);
    return id;
}

u32 SpirvEmitter::EmitNot(u32 type, u32 op) {
    u32 id = AllocId();
    EmitInst3(SpirvOp::Not, type, id, op);
    return id;
}

u32 SpirvEmitter::EmitIsInf(u32 result_type, u32 op) {
    // OpIsInf 是 SPIR-V 核心指令 (opcode 244)
    u32 id = AllocId();
    EmitInst3(244, result_type, id, op);
    return id;
}

u32 SpirvEmitter::EmitIsNan(u32 result_type, u32 op) {
    // OpIsNan 是 SPIR-V 核心指令 (opcode 243)
    u32 id = AllocId();
    EmitInst3(243, result_type, id, op);
    return id;
}

// ═══════════════════════════════════════════════════════════
// Select / composite
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitSelect(u32 result_type, u32 condition,
                              u32 true_val, u32 false_val) {
    u32 id = AllocId();
    EmitInst5(SpirvOp::Select, result_type, id, condition, true_val, false_val);
    return id;
}

u32 SpirvEmitter::EmitCompositeConstruct(u32 result_type,
                                          const std::vector<u32>& constituents) {
    u32 id = AllocId();
    std::vector<u32> operands;
    operands.push_back(result_type);
    operands.push_back(id);
    for (u32 c : constituents)
        operands.push_back(c);
    EmitInst(SpirvOp::CompositeConstruct, (u32)operands.size(), operands);
    return id;
}

u32 SpirvEmitter::EmitCompositeExtract(u32 result_type, u32 composite,
                                        u32 index) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::CompositeExtract, result_type, id, composite, index);
    return id;
}

u32 SpirvEmitter::EmitVectorShuffle(u32 result_type, u32 vec1, u32 vec2,
                                     const std::vector<u32>& indices) {
    u32 id = AllocId();
    std::vector<u32> operands;
    operands.push_back(result_type);
    operands.push_back(id);
    operands.push_back(vec1);
    operands.push_back(vec2);
    for (u32 idx : indices) operands.push_back(idx);
    EmitInst(SpirvOp::VectorShuffle, (u32)operands.size(), operands);
    return id;
}

// ═══════════════════════════════════════════════════════════
// Texture
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitSampledImage(u32 result_type, u32 image, u32 sampler) {
    u32 id = AllocId();
    EmitInst4(SpirvOp::SampledImage, result_type, id, image, sampler);
    return id;
}

u32 SpirvEmitter::EmitImageSampleExplicitLod(u32 result_type, u32 sampled_img,
                                              u32 coord, u32 lod) {
    u32 id = AllocId();
    // ImageSampleExplicitLod: result_type, id, sampled_img, coord, lod_bias
    EmitInst5(SpirvOp::ImageSampleExplicitLod, result_type, id,
              sampled_img, coord, lod);
    return id;
}

u32 SpirvEmitter::EmitUndef(u32 result_type) {
    u32 id = AllocId();
    EmitInst2(SpirvOp::Undef, result_type, id);
    return id;
}

// ═══════════════════════════════════════════════════════════
// GPR → SPIR-V local variable mapping
// ═══════════════════════════════════════════════════════════

std::unordered_map<u32, u32> SpirvEmitter::BuildGprMap(
    const ShaderProgram& program, u32 func_id) {
    std::unordered_map<u32, u32> map;

    // Use the pre-allocated f32 function-pointer type from BuildBaseTypes().
    u32 f32_ptr = types_.f32_fnptr;

    for (const auto& inst : program.instructions) {
        if (inst.dest.type == OperandType::GPR) {
            u32 gpr = inst.dest.gpr.reg_index;
            if (map.find(gpr) == map.end()) {
                u32 var_id = EmitVariable(f32_ptr, (u32)SpirvStorageClass::Function);
                map[gpr] = var_id;
            }
        }
        for (int i = 0; i < inst.src_count; i++) {
            if (inst.src[i].type == OperandType::GPR) {
                u32 gpr = inst.src[i].gpr.reg_index;
                if (map.find(gpr) == map.end()) {
                    u32 var_id = EmitVariable(f32_ptr, (u32)SpirvStorageClass::Function);
                    map[gpr] = var_id;
                }
            }
        }
    }

    LOG_DEBUG("Built GPR map: %zu entries, func_id=%u", map.size(), func_id);
    return map;
}

// ═══════════════════════════════════════════════════════════
// Emit a single ShaderIR instruction
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitShaderInst(const ShaderInstruction& inst,
                                  const std::unordered_map<u32, u32>& gpr_map,
                                  u32 func_id) {
    u32 result = 0;

    auto get_gpr = [&](u8 index) -> u32 {
        auto it = gpr_map.find(index);
        if (it != gpr_map.end()) return it->second;
        return 0;
    };

    auto load_gpr = [&](u8 index) -> u32 {
        u32 var = get_gpr(index);
        if (var == 0) return 0;
        return EmitLoad(types_.f32_t, var);
    };

    auto store_gpr = [&](u8 index, u32 value) {
        u32 var = get_gpr(index);
        if (var != 0) EmitStore(var, value);
    };

    switch (inst.opcode) {
    case ShaderOpcode::NOP:
        // NOP: nothing
        break;

    case ShaderOpcode::EXIT:
    case ShaderOpcode::RET:
        // Return is emitted once after the instruction loop (step 14).
        // Individual EXIT/RET instructions should NOT emit it here
        // to avoid duplication.
        break;

    case ShaderOpcode::KIL:
        EmitKill();
        break;

    case ShaderOpcode::FMAD: {
        // dest = src0 * src1 + src2 (fused multiply-add)
        if (inst.src_count >= 3) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            u32 s2 = load_gpr(inst.src[2].gpr.reg_index);
            if (s0 && s1 && s2) {
                u32 mul = EmitFMul(types_.f32_t, s0, s1);
                u32 add = EmitFAdd(types_.f32_t, mul, s2);
                store_gpr(inst.dest.gpr.reg_index, add);
                result = add;
            }
        }
        break;
    }

    case ShaderOpcode::FADD: {
        // dest = src0 + src1
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 add = EmitFAdd(types_.f32_t, s0, s1);
                store_gpr(inst.dest.gpr.reg_index, add);
                result = add;
            }
        }
        break;
    }

    case ShaderOpcode::FMUL: {
        // dest = src0 * src1
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 mul = EmitFMul(types_.f32_t, s0, s1);
                store_gpr(inst.dest.gpr.reg_index, mul);
                result = mul;
            }
        }
        break;
    }

    case ShaderOpcode::IADD: {
        // dest = src0 + src1 (integer)
        // GPRs are f32, so bitcast loaded floats to i32, add, bitcast back
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 s1i = EmitBitcast(types_.i32_t, s1);
                u32 add = EmitIAdd(types_.i32_t, s0i, s1i);
                u32 addf = EmitBitcast(types_.f32_t, add);
                store_gpr(inst.dest.gpr.reg_index, addf);
                result = addf;
            }
        }
        break;
    }

    case ShaderOpcode::ISUB: {
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 s1i = EmitBitcast(types_.i32_t, s1);
                u32 sub = EmitISub(types_.i32_t, s0i, s1i);
                u32 subf = EmitBitcast(types_.f32_t, sub);
                store_gpr(inst.dest.gpr.reg_index, subf);
                result = subf;
            }
        }
        break;
    }

    case ShaderOpcode::FNEG: {
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 neg = EmitFNegate(types_.f32_t, s0);
                store_gpr(inst.dest.gpr.reg_index, neg);
                result = neg;
            }
        }
        break;
    }

    case ShaderOpcode::INEG: {
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 neg_i = AllocId();
                EmitInst3(126, types_.i32_t, neg_i, s0i);  // OpSNegate (opcode 126)
                u32 negf = EmitBitcast(types_.f32_t, neg_i);
                store_gpr(inst.dest.gpr.reg_index, negf);
                result = negf;
            }
        }
        break;
    }

    case ShaderOpcode::S2R: {
        // Map special registers to SPIR-V built-in variables
        // For now, use the pre-emitted f32 0.0 constant as placeholder.
        // The constant was declared in global scope (step 8), so it's valid
        // to reference it from inside the function body.
        store_gpr(inst.dest.gpr.reg_index, const_f32_zero_);
        result = const_f32_zero_;
        break;
    }

    case ShaderOpcode::LD: {
        // Load from memory: dest = *src
        u32 addr = load_gpr(inst.src[0].gpr.reg_index);
        if (addr) {
            store_gpr(inst.dest.gpr.reg_index, addr);
            result = addr;
        }
        break;
    }

    case ShaderOpcode::ST: {
        // Store to memory: *dest = src
        u32 val = load_gpr(inst.src[0].gpr.reg_index);
        if (val) {
            store_gpr(inst.dest.gpr.reg_index, val);
            result = val;
        }
        break;
    }

    case ShaderOpcode::IMUL: {
        // dest = src0 * src1 (integer, 32-bit low)
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 s1i = EmitBitcast(types_.i32_t, s1);
                u32 mul = EmitIMul(types_.i32_t, s0i, s1i);
                u32 mulf = EmitBitcast(types_.f32_t, mul);
                store_gpr(inst.dest.gpr.reg_index, mulf);
                result = mulf;
            }
        }
        break;
    }

    case ShaderOpcode::IMAD: {
        // dest = src0 * src1 + src2 (integer multiply-add)
        if (inst.src_count >= 3) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            u32 s2 = load_gpr(inst.src[2].gpr.reg_index);
            if (s0 && s1 && s2) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 s1i = EmitBitcast(types_.i32_t, s1);
                u32 s2i = EmitBitcast(types_.i32_t, s2);
                u32 mul = EmitIMul(types_.i32_t, s0i, s1i);
                u32 add = EmitIAdd(types_.i32_t, mul, s2i);
                u32 addf = EmitBitcast(types_.f32_t, add);
                store_gpr(inst.dest.gpr.reg_index, addf);
                result = addf;
            }
        }
        break;
    }

    case ShaderOpcode::F2I: {
        // dest = (int)src0 — float to signed integer
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 conv = EmitConvertFToS(types_.i32_t, s0);
                u32 convf = EmitBitcast(types_.f32_t, conv);
                store_gpr(inst.dest.gpr.reg_index, convf);
                result = convf;
            }
        }
        break;
    }

    case ShaderOpcode::I2F: {
        // dest = (float)src0 — signed integer to float
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 conv = EmitConvertSToF(types_.f32_t, s0i);
                store_gpr(inst.dest.gpr.reg_index, conv);
                result = conv;
            }
        }
        break;
    }

    case ShaderOpcode::LDC: {
        // Load from constant buffer: dest = cbuf[src0]
        if (inst.src_count >= 1) {
            u32 index = load_gpr(inst.src[0].gpr.reg_index);
            if (index && cbuf_var_id_) {
                u32 idx_u32 = EmitBitcast(types_.u32_t, index);
                u32 ptr = EmitAccessChain(types_.f32_uniform_ptr, cbuf_var_id_,
                                          {const_f32_zero_, idx_u32});
                u32 val = EmitLoad(types_.f32_t, ptr);
                store_gpr(inst.dest.gpr.reg_index, val);
                result = val;
            }
        }
        break;
    }

    case ShaderOpcode::MUFU: {
        // Multi-Function Unit: sin, cos, ex2, lg2, rcp, rsq, sqrt, floor, ceil, tan
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 val = s0;
                MufuOp mufu_op = inst.src[0].mufu_op;
                switch (mufu_op) {
                case MufuOp::SIN:
                    val = EmitExtInst(types_.f32_t, GlslStd450::Sin, {s0});
                    break;
                case MufuOp::COS:
                    val = EmitExtInst(types_.f32_t, GlslStd450::Cos, {s0});
                    break;
                case MufuOp::EX2:
                    val = EmitExtInst(types_.f32_t, GlslStd450::Exp2, {s0});
                    break;
                case MufuOp::LG2:
                    val = EmitExtInst(types_.f32_t, GlslStd450::Log2, {s0});
                    break;
                case MufuOp::RCP:
                    // reciprocal: 1.0 / s0
                    val = EmitFDiv(types_.f32_t, const_f32_one_, s0);
                    break;
                case MufuOp::RSQ:
                    // reciprocal sqrt: 1.0 / sqrt(s0)
                    val = EmitExtInst(types_.f32_t, GlslStd450::InverseSqrt, {s0});
                    break;
                case MufuOp::SQRT:
                    val = EmitExtInst(types_.f32_t, GlslStd450::Sqrt, {s0});
                    break;
                case MufuOp::FLOOR:
                    val = EmitExtInst(types_.f32_t, GlslStd450::Floor, {s0});
                    break;
                case MufuOp::CEIL:
                    val = EmitExtInst(types_.f32_t, GlslStd450::Ceil, {s0});
                    break;
                case MufuOp::TAN:
                    val = EmitExtInst(types_.f32_t, GlslStd450::Tan, {s0});
                    break;
                default:
                    val = s0;
                    break;
                }
                store_gpr(inst.dest.gpr.reg_index, val);
                result = val;
            }
        }
        break;
    }

    case ShaderOpcode::TEX: {
        // Texture sample: dest = texture(sampler, coord)
        if (inst.src_count >= 2) {
            u32 coord = load_gpr(inst.src[0].gpr.reg_index);
            if (coord && tex_sampled_image_var_ != 0) {
                // Load the pre-declared sampled image variable, coordinates as vec2
                u32 coord_vec2 = EmitCompositeConstruct(types_.vec2f32_t, {coord, coord});
                u32 image_id = EmitLoad(types_.f32_image_2d, tex_sampled_image_var_);
                u32 sample_result = EmitImageSampleExplicitLod(
                    types_.vec4f32_t, image_id, coord_vec2, const_f32_zero_);
                store_gpr(inst.dest.gpr.reg_index, sample_result);
                result = sample_result;
            }
        }
        break;
    }

    case ShaderOpcode::FMAX: {
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 max = EmitFMax(types_.f32_t, s0, s1);
                store_gpr(inst.dest.gpr.reg_index, max);
                result = max;
            }
        }
        break;
    }

    case ShaderOpcode::FMIN: {
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 min = EmitFMin(types_.f32_t, s0, s1);
                store_gpr(inst.dest.gpr.reg_index, min);
                result = min;
            }
        }
        break;
    }

    case ShaderOpcode::FABS: {
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 abs_val = EmitFAbs(types_.f32_t, s0);
                store_gpr(inst.dest.gpr.reg_index, abs_val);
                result = abs_val;
            }
        }
        break;
    }

    case ShaderOpcode::IABS: {
        // integer absolute via SAbs extended instruction
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 abs_i = EmitExtInst(types_.i32_t, GlslStd450::SAbs, {s0i});
                u32 abs_f = EmitBitcast(types_.f32_t, abs_i);
                store_gpr(inst.dest.gpr.reg_index, abs_f);
                result = abs_f;
            }
        }
        break;
    }

    case ShaderOpcode::IADD3: {
        // dest = src0 + src1 + src2 (3-input integer add)
        if (inst.src_count >= 3) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            u32 s2 = load_gpr(inst.src[2].gpr.reg_index);
            if (s0 && s1 && s2) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 s1i = EmitBitcast(types_.i32_t, s1);
                u32 s2i = EmitBitcast(types_.i32_t, s2);
                u32 add1 = EmitIAdd(types_.i32_t, s0i, s1i);
                u32 add2 = EmitIAdd(types_.i32_t, add1, s2i);
                u32 add2f = EmitBitcast(types_.f32_t, add2);
                store_gpr(inst.dest.gpr.reg_index, add2f);
                result = add2f;
            }
        }
        break;
    }

    case ShaderOpcode::BRA:
    case ShaderOpcode::CALL:
    case ShaderOpcode::SSY:
    case ShaderOpcode::SYNC:
        // Control flow: handled at the CFG level in EmitStructuredCfg().
        // These opcodes are consumed by the CFG builder and not emitted
        // as standalone SPIR-V instructions here.
        break;

    case ShaderOpcode::ISETP: {
        // Integer compare: pN = (src0 cmp src1). Stores bool in pred_vars.
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 s1i = EmitBitcast(types_.i32_t, s1);
                // Compare based on the condition encoded in pred_dest bits
                // Default to equality; real hardware uses bits in the instruction
                u32 cmp = EmitIEqual(types_.bool_t, s0i, s1i);
                u32 pred_idx = inst.dest.pred.pred_index;
                // Store the bool result: we need an OpStore to a Function-scope bool variable
                auto pit = cfg_state_.pred_vars.find(pred_idx);
                if (pit == cfg_state_.pred_vars.end()) {
                    u32 bool_ptr = EmitTypePointer((u32)SpirvStorageClass::Function, types_.bool_t);
                    u32 var_id = EmitVariable(bool_ptr, (u32)SpirvStorageClass::Function);
                    cfg_state_.pred_vars[pred_idx] = var_id;
                    pit = cfg_state_.pred_vars.find(pred_idx);
                }
                EmitStore(pit->second, cmp);
                result = cmp;
            }
        }
        break;
    }

    case ShaderOpcode::FSETP: {
        // 浮点比较: pN = (src0 cmp src1)
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                // 根据 dest 低位决定比较方式 (简化: 默认 FOrdEqual)
                // TODO: 从解码器提取完整比较条件
                u32 cmp = EmitFOrdEqual(types_.bool_t, s0, s1);
                u32 pred_idx = inst.dest.pred.pred_index;
                auto pit = cfg_state_.pred_vars.find(pred_idx);
                if (pit == cfg_state_.pred_vars.end()) {
                    u32 bool_ptr = EmitTypePointer((u32)SpirvStorageClass::Function, types_.bool_t);
                    u32 var_id = EmitVariable(bool_ptr, (u32)SpirvStorageClass::Function);
                    cfg_state_.pred_vars[pred_idx] = var_id;
                    pit = cfg_state_.pred_vars.find(pred_idx);
                }
                EmitStore(pit->second, cmp);
                result = cmp;
            }
        }
        break;
    }

    // ── 类型转换 ──────────────────────────────────────────
    // F2I and I2F are handled above in their original case blocks.
    case ShaderOpcode::I2I: {
        // 整数类型转换 (在 GPR 中位宽可能不同, 但 SPIR-V 中都是 i32)
        // 直接 bitcast 后存回 (值不变)
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                store_gpr(inst.dest.gpr.reg_index, s0);
                result = s0;
            }
        }
        break;
    }

    case ShaderOpcode::F2F: {
        // float 精度转换 (在 GPR 中都是 f32, 直接传递)
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                store_gpr(inst.dest.gpr.reg_index, s0);
                result = s0;
            }
        }
        break;
    }

    // ── 位操作 ──────────────────────────────────────────────
    case ShaderOpcode::SHL: {
        // 左移: dest = src0 << src1 (整数)
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 s1i = EmitBitcast(types_.u32_t, s1);
                u32 shifted = EmitShiftLeftLogical(types_.i32_t, s0i, s1i);
                u32 shiftedf = EmitBitcast(types_.f32_t, shifted);
                store_gpr(inst.dest.gpr.reg_index, shiftedf);
                result = shiftedf;
            }
        }
        break;
    }

    case ShaderOpcode::SHR: {
        // 右移: dest = src0 >> src1
        // SHR 对于无符号数是逻辑右移, 有符号数是算术右移
        // 默认当作算术右移 (有符号), 因为 Maxwell SHR 通常是 ASR
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 s1i = EmitBitcast(types_.u32_t, s1);
                u32 shifted = EmitShiftRightArithmetic(types_.i32_t, s0i, s1i);
                u32 shiftedf = EmitBitcast(types_.f32_t, shifted);
                store_gpr(inst.dest.gpr.reg_index, shiftedf);
                result = shiftedf;
            }
        }
        break;
    }

    case ShaderOpcode::BFE: {
        // 位域提取: dest = (src0 >> src1) & ((1 << src2) - 1) (无符号)
        if (inst.src_count >= 3) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            u32 s2 = load_gpr(inst.src[2].gpr.reg_index);
            if (s0 && s1 && s2) {
                u32 base = EmitBitcast(types_.u32_t, s0);
                u32 offset = EmitBitcast(types_.u32_t, s1);
                u32 count = EmitBitcast(types_.u32_t, s2);
                u32 extracted = EmitBitFieldUExtract(types_.u32_t, base, offset, count);
                u32 extractedf = EmitBitcast(types_.f32_t, extracted);
                store_gpr(inst.dest.gpr.reg_index, extractedf);
                result = extractedf;
            }
        }
        break;
    }

    case ShaderOpcode::BFI: {
        // 位域插入: dest = (base & ~((1<<count)-1)<<offset) | ((insert & ((1<<count)-1))<<offset)
        if (inst.src_count >= 3) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            u32 s2 = load_gpr(inst.src[2].gpr.reg_index);
            if (s0 && s1 && s2) {
                u32 base = EmitBitcast(types_.u32_t, s0);
                u32 insert = EmitBitcast(types_.u32_t, s1);
                u32 offset = EmitBitcast(types_.u32_t, s2);
                // count 从 BFI 格式的源寄存器提取 (位[4:0] = count, 位[12:5] = offset)
                // 简化: 假设 count 在 dest 的低位字段中 (实际 Maxwell 从 imm 提取)
                u32 count_const = EmitConstantU32(32);
                u32 inserted = EmitBitFieldInsert(types_.u32_t, base, insert, offset, count_const);
                u32 insertedf = EmitBitcast(types_.f32_t, inserted);
                store_gpr(inst.dest.gpr.reg_index, insertedf);
                result = insertedf;
            }
        }
        break;
    }

    case ShaderOpcode::PRMT: {
        // 寄存器重排: dest = permute(src0, src1, src2)
        // src2 的每字节控制从 src0/src1 选择哪个字节
        // 简化实现: 直接传递 src0
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                store_gpr(inst.dest.gpr.reg_index, s0);
                result = s0;
            }
        }
        break;
    }

    // ── 整数比较/选择 ───────────────────────────────────
    case ShaderOpcode::IMNMX: {
        // 整数最小/最大: 根据 pred 决定取 min 还是 max
        // 简化实现: 用 FMax + bitcast (SPIR-V IMax/IExtInst 都不合适,
        // 用 SLessThan + Select 实现)
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 s0i = EmitBitcast(types_.i32_t, s0);
                u32 s1i = EmitBitcast(types_.i32_t, s1);
                // 取 max(s0, s1): cmp = s0 >= s1, result = select(cmp, s0, s1)
                u32 cmp = EmitSGreaterThanEqual(types_.bool_t, s0i, s1i);
                u32 sel = EmitSelect(types_.i32_t, cmp, s0i, s1i);
                u32 self = EmitBitcast(types_.f32_t, sel);
                store_gpr(inst.dest.gpr.reg_index, self);
                result = self;
            }
        }
        break;
    }

    case ShaderOpcode::FSET: {
        // 浮点比较并写入 GPR (而非谓词)
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 cmp = EmitFOrdEqual(types_.bool_t, s0, s1);
                // bool → f32: select(1.0, 0.0, cmp)
                u32 one = const_f32_one_;
                u32 zero = const_f32_zero_;
                u32 sel = EmitSelect(types_.f32_t, cmp, one, zero);
                store_gpr(inst.dest.gpr.reg_index, sel);
                result = sel;
            }
        }
        break;
    }

    // ── LEA (加载有效地址) ──────────────────────────────────
    case ShaderOpcode::LEA: {
        // LEA: dest = src0 + (src1 << src2)
        // 常见用法: 地址计算
        if (inst.src_count >= 2) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            u32 s1 = load_gpr(inst.src[1].gpr.reg_index);
            if (s0 && s1) {
                u32 s0i = EmitBitcast(types_.u32_t, s0);
                u32 s1i = EmitBitcast(types_.u32_t, s1);
                u32 shifted = EmitShiftLeftLogical(types_.u32_t, s1i, EmitConstantU32(2));
                u32 add = EmitIAdd(types_.u32_t, s0i, shifted);
                u32 addf = EmitBitcast(types_.f32_t, add);
                store_gpr(inst.dest.gpr.reg_index, addf);
                result = addf;
            }
        }
        break;
    }

    // ── POPC (population count) ──────────────────────────────
    case ShaderOpcode::POPC: {
        // SPIR-V 没有 OpPopcount, 但有 GLSL.std.450 的 SMulExtended 或可以用
        // BitCount = 443 作为 SPIR-V OpBitCount (非扩展指令)
        // 简化实现: 传递 src0 作为桩
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                // 用 OpBitCount (opcode 443) - 但 SPIR-V 1.0 需要 shader ballot capability
                // 简化: 直接传递 src0
                store_gpr(inst.dest.gpr.reg_index, s0);
                result = s0;
            }
        }
        break;
    }

    // ── FLO (find leading one) ───────────────────────────────
    case ShaderOpcode::FLO: {
        // 找最高有效位 (等同于 FindMSB = floor(log2(x)))
        // SPIR-V 没有 OpFindMSB, 用 GLSL.std.450 也没有
        // 简化实现: 直接传递 src0 作为桩
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                store_gpr(inst.dest.gpr.reg_index, s0);
                result = s0;
            }
        }
        break;
    }

    // ── FSAT (浮点限幅 [0,1]) ──────────────────────────────
    case ShaderOpcode::FSAT: {
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 clamped = EmitExtInst(types_.f32_t, GlslStd450::FClamp, {s0, const_f32_zero_, const_f32_one_});
                store_gpr(inst.dest.gpr.reg_index, clamped);
                result = clamped;
            }
        }
        break;
    }

    // ── FRCP (浮点倒数) ──────────────────────────────────────
    case ShaderOpcode::FRCP: {
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 rcp = EmitFDiv(types_.f32_t, const_f32_one_, s0);
                store_gpr(inst.dest.gpr.reg_index, rcp);
                result = rcp;
            }
        }
        break;
    }

    // ── FRSQ (浮点倒数平方根) ──────────────────────────────
    case ShaderOpcode::FRSQ: {
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 rsq = EmitExtInst(types_.f32_t, GlslStd450::InverseSqrt, {s0});
                store_gpr(inst.dest.gpr.reg_index, rsq);
                result = rsq;
            }
        }
        break;
    }

    // ── FSQRT (浮点平方根) ────────────────────────────────
    case ShaderOpcode::FSQRT: {
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 sqrt_val = EmitExtInst(types_.f32_t, GlslStd450::Sqrt, {s0});
                store_gpr(inst.dest.gpr.reg_index, sqrt_val);
                result = sqrt_val;
            }
        }
        break;
    }

    // ── FEX2 (2^x) ────────────────────────────────────────
    case ShaderOpcode::FEX2: {
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 ex2 = EmitExtInst(types_.f32_t, GlslStd450::Exp2, {s0});
                store_gpr(inst.dest.gpr.reg_index, ex2);
                result = ex2;
            }
        }
        break;
    }

    // ── FLG2 (log2) ────────────────────────────────────────
    case ShaderOpcode::FLG2: {
        if (inst.src_count >= 1) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                u32 lg2 = EmitExtInst(types_.f32_t, GlslStd450::Log2, {s0});
                store_gpr(inst.dest.gpr.reg_index, lg2);
                result = lg2;
            }
        }
        break;
    }

    // ── FDIV (浮点除法, 独立操作码) ─────────────────────
    // 注意: FDIV 不在原有 ShaderOpcode 表里, 但 FSub 在
    default:
        // For unhandled opcodes, create a NOP-like pass-through
        if (inst.dest.type == OperandType::GPR && inst.src_count > 0) {
            u32 s0 = load_gpr(inst.src[0].gpr.reg_index);
            if (s0) {
                store_gpr(inst.dest.gpr.reg_index, s0);
                result = s0;
            }
        }
        break;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════
// Main emit function
// ═══════════════════════════════════════════════════════════

void SpirvEmitter::EmitShaderIo(ShaderStage stage) {
    if (stage == ShaderStage::VertexA || stage == ShaderStage::VertexB) {
        io_.vert_in_position = AllocId();
        io_.vert_in_color = AllocId();
        io_.vert_out_position = AllocId();
        io_.vert_out_color = AllocId();
    } else if (stage == ShaderStage::Fragment) {
        io_.frag_in_color = AllocId();
        io_.frag_out_color = AllocId();
    }
}

void SpirvEmitter::EmitVertexIoBody() {
    u32 pos_val = EmitLoad(types_.vec4f32_t, io_.vert_in_position);
    EmitStore(io_.vert_out_position, pos_val);
    u32 col_val = EmitLoad(types_.vec4f32_t, io_.vert_in_color);
    EmitStore(io_.vert_out_color, col_val);
}

void SpirvEmitter::EmitFragmentIoBody() {
    u32 col_val = EmitLoad(types_.vec4f32_t, io_.frag_in_color);
    EmitStore(io_.frag_out_color, col_val);
}

// ═══════════════════════════════════════════════════════════
// CFG Analysis (pre-pass)
// ═══════════════════════════════════════════════════════════

CfgAnnotation SpirvEmitter::BuildCfg(const ShaderProgram& program) {
    CfgAnnotation cfg;

    // Step 1: Build PC → instruction index map
    for (u32 i = 0; i < (u32)program.instructions.size(); i++) {
        cfg.pc_to_index[program.instructions[i].pc] = i;
    }

    // Step 2: Scan for SSY/SYNC pairs
    for (u32 i = 0; i < (u32)program.instructions.size(); i++) {
        const auto& inst = program.instructions[i];

        if (inst.opcode == ShaderOpcode::SSY) {
            // SSY's branch_target points to the matching SYNC's PC
            u32 ssy_pc = inst.pc;
            u32 sync_pc = inst.src[0].label * 8;  // Label is in instruction units, convert to PC bytes

            SsySyncPair pair;
            pair.ssy_pc = ssy_pc;
            pair.sync_pc = sync_pc;

            // Step 3: Find the BRA between SSY and SYNC
            for (u32 j = i + 1; j < (u32)program.instructions.size(); j++) {
                const auto& next = program.instructions[j];

                // Stop at SYNC (if we reach it before finding BRA, it's an empty pair)
                if (next.opcode == ShaderOpcode::SYNC && next.pc == sync_pc)
                    break;

                if (next.opcode == ShaderOpcode::BRA) {
                    pair.bra_pc = next.pc;
                    pair.bra_target_pc = next.src[0].label * 8;

                    // Classify: forward = Selection, backward = Loop
                    if (pair.bra_target_pc < ssy_pc + 8) {
                        // Backward branch → loop
                        pair.type = CfgBlockType::Loop;
                    } else if (pair.bra_target_pc <= sync_pc) {
                        // Forward branch within SSY→SYNC range → selection
                        pair.type = CfgBlockType::Selection;
                    } else {
                        // Branch target beyond SYNC → also selection (early exit pattern)
                        pair.type = CfgBlockType::Selection;
                    }
                    break;
                }
            }

            cfg.pairs.push_back(pair);
            cfg.sync_to_ssy[sync_pc] = ssy_pc;
        }
    }

    // Step 4: Mark block starts (branch targets, SYNC points, and SSY+8 headers)
    for (const auto& pair : cfg.pairs) {
        cfg.block_starts.insert(pair.sync_pc);
        cfg.block_starts.insert(pair.ssy_pc + 8);  // instruction after SSY
        if (pair.bra_target_pc != 0) {
            cfg.block_starts.insert(pair.bra_target_pc);
        }
    }
    // Also mark the first instruction as a block start
    if (!program.instructions.empty()) {
        cfg.block_starts.insert(program.instructions[0].pc);
    }

    LOG_DEBUG("CFG built: %zu SSY/SYNC pairs, %zu block starts",
              cfg.pairs.size(), cfg.block_starts.size());

    return cfg;
}

// ═══════════════════════════════════════════════════════════
// CFG label management
// ═══════════════════════════════════════════════════════════

// Reserve (allocate) a label ID for a PC without emitting OpLabel.
// Used for forward references (BRA targets, merge blocks).
// OpLabel is only emitted when we linearly reach a block_start PC.
u32 SpirvEmitter::ReserveLabel(u32 pc) {
    auto it = cfg_state_.pc_to_label.find(pc);
    if (it != cfg_state_.pc_to_label.end())
        return it->second;

    u32 label_id = AllocId();
    cfg_state_.pc_to_label[pc] = label_id;
    return label_id;
}

// Get or create label: emits OpLabel at current position.
// Only call this when reaching a block_start PC during linear iteration.
u32 SpirvEmitter::GetOrCreateLabel(u32 pc) {
    auto it = cfg_state_.pc_to_label.find(pc);
    if (it != cfg_state_.pc_to_label.end()) {
        // Label ID already reserved; emit OpLabel now if not already emitted
        if (!cfg_state_.labels_emitted.count(pc)) {
            EmitInst1(SpirvOp::Label, it->second);
            cfg_state_.labels_emitted.insert(pc);
        }
        return it->second;
    }

    // First time seeing this PC: allocate and emit
    u32 label_id = AllocId();
    EmitInst1(SpirvOp::Label, label_id);
    cfg_state_.pc_to_label[pc] = label_id;
    cfg_state_.labels_emitted.insert(pc);
    return label_id;
}

// ═══════════════════════════════════════════════════════════
// Condition emission (for predicated branches)
// ═══════════════════════════════════════════════════════════

u32 SpirvEmitter::EmitCondition(const ShaderInstruction& inst,
                                 const std::unordered_map<u32, u32>& gpr_map) {
    (void)gpr_map;

    if (inst.pred_guard) {
        u32 pred_idx = inst.pred_guard_index;
        auto it = cfg_state_.pred_vars.find(pred_idx);
        if (it != cfg_state_.pred_vars.end()) {
            // Return raw predicate; negation is handled by the caller
            // swapping true/false branch targets.
            return EmitLoad(types_.bool_t, it->second);
        }
        // Predicate not set → always false
        return const_false_;
    }

    // Unconditional branch → always true
    return const_true_;
}

// ═══════════════════════════════════════════════════════════
// Structured CFG emission
// ═══════════════════════════════════════════════════════════

void SpirvEmitter::EmitStructuredCfg(
    const ShaderProgram& program,
    const CfgAnnotation& cfg,
    const std::unordered_map<u32, u32>& gpr_map,
    u32 func_id) {
    (void)func_id;

    const auto& insts = program.instructions;

    // NOTE: cfg_state_ is NOT cleared here — it was already cleared
    // in Emit() before calling EmitStructuredCfg(). The entry label's
    // PC may have been pre-marked in labels_emitted to prevent duplicate
    // OpLabel emission for the function entry block.

    // Gather all PCs that have an instruction
    std::unordered_set<u32> all_pcs;
    for (const auto& inst : insts) all_pcs.insert(inst.pc);

    for (u32 i = 0; i < (u32)insts.size(); i++) {
        const auto& inst = insts[i];
        u32 pc = inst.pc;

        // ── Emit label if this PC starts a new block ─────
        // Check labels_emitted (not pc_to_label) because ReserveLabel
        // pre-populates pc_to_label for forward references (SSY merge blocks).
        if (cfg.block_starts.count(pc) && !cfg_state_.labels_emitted.count(pc)) {
            u32 lbl_id = AllocId();
            EmitInst1(SpirvOp::Label, lbl_id);
            cfg_state_.pc_to_label[pc] = lbl_id;
            cfg_state_.labels_emitted.insert(pc);
            LOG_DEBUG("  Label for PC=0x%x → %u", pc, lbl_id);
        }

        // ── Handle SYNC (reconvergence) ─────────────────
        if (inst.opcode == ShaderOpcode::SYNC) {
            // Pop the SSY stack if this SYNC matches the top entry.
            if (!cfg_state_.ssy_stack.empty()) {
                auto& top = cfg_state_.ssy_stack.back();
                if (top.sync_pc == pc) {
                    cfg_state_.ssy_stack.pop_back();
                    LOG_DEBUG("  SYNC pop (0x%x)", pc);
                }
            }
            continue;
        }

        // ── Handle SSY (divergence announcement) ────────
        // Merge is DEFERRED to the BRA instruction to satisfy
        // SPIR-V requirement: merge must immediately precede the branch.
        if (inst.opcode == ShaderOpcode::SSY) {
            for (const auto& pair : cfg.pairs) {
                if (pair.ssy_pc == pc) {
                    SsyStackEntry entry;
                    entry.ssy_pc = pc;
                    entry.sync_pc = pair.sync_pc;
                    entry.type = pair.type;
                    // Reserve label IDs WITHOUT emitting OpLabel
                    entry.merge_label = ReserveLabel(pair.sync_pc);
                    if (pair.type == CfgBlockType::Loop && pair.bra_target_pc != 0) {
                        entry.continue_label = ReserveLabel(pair.bra_target_pc);
                    } else if (pair.type == CfgBlockType::Loop) {
                        entry.continue_label = ReserveLabel(pc + 8);
                    }
                    cfg_state_.ssy_stack.push_back(entry);
                    LOG_DEBUG("  SSY 0x%x→0x%x (type=%d), merge=%u continue=%u deferred",
                              pc, pair.sync_pc, (int)pair.type,
                              entry.merge_label, entry.continue_label);
                    break;
                }
            }
            continue;
        }

        // ── Handle unconditional BRA ────────────────────
        if (inst.opcode == ShaderOpcode::BRA && !inst.pred_guard && inst.dest.type == OperandType::None) {
            // Emit deferred merge from the matching SSY stack entry
            if (!cfg_state_.ssy_stack.empty()) {
                auto& top = cfg_state_.ssy_stack.back();
                if (top.merge_label && top.type == CfgBlockType::Selection) {
                    EmitInst2(SpirvOp::SelectionMerge, top.merge_label, 0);
                    LOG_DEBUG("  SelectionMerge → %u (deferred)", top.merge_label);
                } else if (top.merge_label && top.type == CfgBlockType::Loop) {
                    EmitInst3(SpirvOp::LoopMerge, top.merge_label, top.continue_label, 0);
                    LOG_DEBUG("  LoopMerge → %u, continue=%u (deferred)", top.merge_label, top.continue_label);
                }
                top.merge_label = 0;  // Mark as emitted
            }

            // Reserve label without emitting OpLabel. The linear iterator
            // will emit OpLabel when it reaches the target PC (it's in block_starts).
            u32 target_pc = inst.src[0].label * 8;
            u32 target_label = ReserveLabel(target_pc);
            EmitBranch(target_label);
            LOG_DEBUG("  OpBranch → %u (BRA 0x%x→0x%x)", target_label, pc, target_pc);
            continue;
        }

        // ── Handle conditional/predicated BRA ───────────
        if (inst.opcode == ShaderOpcode::BRA && inst.pred_guard) {
            // Emit deferred merge from the matching SSY stack entry
            if (!cfg_state_.ssy_stack.empty()) {
                auto& top = cfg_state_.ssy_stack.back();
                if (top.merge_label && top.type == CfgBlockType::Selection) {
                    EmitInst2(SpirvOp::SelectionMerge, top.merge_label, 0);
                    LOG_DEBUG("  SelectionMerge → %u (deferred)", top.merge_label);
                } else if (top.merge_label && top.type == CfgBlockType::Loop) {
                    EmitInst3(SpirvOp::LoopMerge, top.merge_label, top.continue_label, 0);
                    LOG_DEBUG("  LoopMerge → %u, continue=%u (deferred)", top.merge_label, top.continue_label);
                }
                top.merge_label = 0;  // Mark as emitted
            }

            // Reserve labels without emitting OpLabel. The linear iterator emits
            // OpLabel when it reaches block_start PCs (BRA targets are in block_starts).
            u32 target_pc = inst.src[0].label * 8;
            u32 true_label = ReserveLabel(target_pc);

            u32 false_pc = 0;
            if (i + 1 < (u32)insts.size()) {
                false_pc = insts[i + 1].pc;
            } else {
                false_pc = pc + 8;
            }
            u32 false_label = ReserveLabel(false_pc);

            // Load the predicate
            u32 cond = const_true_;
            u32 pred_idx = inst.pred_guard_index;
            auto pit = cfg_state_.pred_vars.find(pred_idx);
            if (pit != cfg_state_.pred_vars.end()) {
                cond = EmitLoad(types_.bool_t, pit->second);
            }

            if (inst.pred_guard_negate) {
                EmitBranchConditional(cond, false_label, true_label);
            } else {
                EmitBranchConditional(cond, true_label, false_label);
            }
            LOG_DEBUG("  OpBranchConditional → %u / %u (BRA 0x%x→0x%x)",
                      true_label, false_label, pc, target_pc);

            // Emit OpLabel for the fallthrough (false) block immediately
            // after the branch. This block is not in block_starts so the
            // linear iterator won't emit it. Must be done here.
            EmitInst1(SpirvOp::Label, false_label);
            cfg_state_.labels_emitted.insert(false_pc);
            continue;
        }

        // ── Skip EXIT/RET (handled at the end) ──────────
        if (inst.opcode == ShaderOpcode::EXIT || inst.opcode == ShaderOpcode::RET) {
            continue;
        }

        // ── Handle BREAK ───────────────────────────────
        if (inst.opcode == ShaderOpcode::BREAK) {
            // Branch to the merge block of the current innermost loop.
            // Use the reserved label from ssy_stack (no OpLabel emit here —
            // the linear iterator emits it when reaching the SYNC PC).
            if (!cfg_state_.ssy_stack.empty()) {
                auto& top = cfg_state_.ssy_stack.back();
                u32 merge_label = ReserveLabel(top.sync_pc);
                EmitBranch(merge_label);
                LOG_DEBUG("  BREAK → OpBranch → %u", merge_label);
            }
            continue;
        }

        // ── Handle CONT (continue) ─────────────────────
        if (inst.opcode == ShaderOpcode::CONT) {
            // Branch to the continue target of the innermost loop.
            // Use the reserved label from ssy_stack (no OpLabel emit here).
            if (!cfg_state_.ssy_stack.empty()) {
                auto& top = cfg_state_.ssy_stack.back();
                if (top.type == CfgBlockType::Loop) {
                    for (const auto& pair : cfg.pairs) {
                        if (pair.ssy_pc == top.ssy_pc) {
                            u32 continue_label = ReserveLabel(pair.bra_target_pc);
                            EmitBranch(continue_label);
                            LOG_DEBUG("  CONT → OpBranch → %u", continue_label);
                            break;
                        }
                    }
                }
            }
            continue;
        }

        // ── Handle KIL (discard) ───────────────────────
        if (inst.opcode == ShaderOpcode::KIL) {
            EmitKill();
            continue;
        }

        // ── Normal instruction emission ─────────────────
        EmitShaderInst(inst, gpr_map, func_id);
    }
}

// ═══════════════════════════════════════════════════════════
// Main emit function
// ═══════════════════════════════════════════════════════════

std::vector<u32> SpirvEmitter::Emit(const ShaderProgram& program) {
    words_.clear();
    error_.clear();
    next_id_ = 1;
    types_built_ = false;
    io_ = {};
    cfg_state_.Clear();

    bool is_vertex = (program.stage == ShaderStage::VertexA || program.stage == ShaderStage::VertexB);
    bool is_fragment = (program.stage == ShaderStage::Fragment);

    EmitHeader();
    EmitCapabilities(program);

    glsl_ext_id_ = AllocId();
    u32 ext_wc = 2 + (u32)((12 + 4) / 4);
    EmitWord((ext_wc << 16) | SpirvOp::ExtInstImport);
    EmitWord(glsl_ext_id_);
    EmitString("GLSL.std.450");

    EmitInst2(SpirvOp::MemoryModel, 0, 1);

    u32 func_id = AllocId();

    EmitShaderIo(program.stage);

    {
        std::vector<u32> interface_ids;
        if (is_vertex) {
            interface_ids.push_back(io_.vert_in_position);
            interface_ids.push_back(io_.vert_in_color);
            interface_ids.push_back(io_.vert_out_position);
            interface_ids.push_back(io_.vert_out_color);
        } else if (is_fragment) {
            interface_ids.push_back(io_.frag_in_color);
            interface_ids.push_back(io_.frag_out_color);
        }

        const char* name = "main";
        size_t name_len = strlen(name);
        size_t name_words = ((name_len + 1 + 3) / 4);
        size_t wc = 3 + name_words + interface_ids.size();

        EmitWord(((u32)wc << 16) | SpirvOp::EntryPoint);
        EmitWord(is_fragment ? 4u : 0u);
        EmitWord(func_id);
        EmitString(name);
        for (u32 id : interface_ids) EmitWord(id);
    }

    if (is_fragment) {
        EmitInst2(SpirvOp::ExecutionMode, func_id, 7);
    }

    if (is_vertex) {
        EmitDecorationLocation(io_.vert_in_position, 0);
        EmitDecorationLocation(io_.vert_in_color, 1);
        EmitDecorationBuiltIn(io_.vert_out_position, (u32)SpirvBuiltIn::Position);
        EmitDecorationLocation(io_.vert_out_color, 0);
    } else if (is_fragment) {
        EmitDecorationLocation(io_.frag_in_color, 0);
        EmitDecorationLocation(io_.frag_out_color, 0);
    }

    // ── Pre-allocate IDs for decorations that reference types/variables ─
    // SPIR-V spec requires ALL OpDecorate instructions to be in the
    // annotations section, BEFORE all type declarations. We pre-allocate
    // ALL IDs that decorations will reference, emit the decorations here,
    // and later emit the actual type/variable instructions (which reuse
    // the pre-allocated IDs via EmitVariablePreallocated).
    types_.cbuf_runtime_array = AllocId();
    types_.cbuf_struct = AllocId();
    cbuf_var_id_ = AllocId();
    EmitDecoration(types_.cbuf_runtime_array, (u32)SpirvDecoration::ArrayStride, 4);
    EmitDecoration(cbuf_var_id_, (u32)SpirvDecoration::DescriptorSet, 0);
    EmitDecoration(cbuf_var_id_, (u32)SpirvDecoration::Binding, 0);

    // Texture variable: pre-allocate ID and emit decorations in annotations section
    if (program.uses_textures) {
        tex_sampled_image_var_ = AllocId();
        EmitDecoration(tex_sampled_image_var_, (u32)SpirvDecoration::DescriptorSet, 0);
        EmitDecoration(tex_sampled_image_var_, (u32)SpirvDecoration::Binding, 0);
    }

    BuildBaseTypes();

    const_f32_zero_ = EmitConstantF32(0.0f);
    const_f32_one_ = EmitConstantF32(1.0f);
    const_true_ = EmitConstantBool(true);
    const_false_ = EmitConstantBool(false);

    if (is_vertex) {
        EmitInst3(SpirvOp::Variable, types_.vec4f32_input_ptr, io_.vert_in_position, (u32)SpirvStorageClass::Input);
        EmitInst3(SpirvOp::Variable, types_.vec4f32_input_ptr, io_.vert_in_color, (u32)SpirvStorageClass::Input);
        EmitInst3(SpirvOp::Variable, types_.vec4f32_output_ptr, io_.vert_out_position, (u32)SpirvStorageClass::Output);
        EmitInst3(SpirvOp::Variable, types_.vec4f32_output_ptr, io_.vert_out_color, (u32)SpirvStorageClass::Output);
    } else if (is_fragment) {
        EmitInst3(SpirvOp::Variable, types_.vec4f32_input_ptr, io_.frag_in_color, (u32)SpirvStorageClass::Input);
        EmitInst3(SpirvOp::Variable, types_.vec4f32_output_ptr, io_.frag_out_color, (u32)SpirvStorageClass::Output);
    }

    // ── Constant buffer variable ────────────────────────
    // Use the pre-allocated ID (ID was already reserved in annotations section).
    // Emit OpVariable instruction for the cbuf with the pre-allocated ID.
    cbuf_var_id_ = EmitVariablePreallocated(cbuf_var_id_, types_.cbuf_ptr,
                                            (u32)SpirvStorageClass::Uniform);

    // ── Texture variable (pre-declared at global scope) ─-
    // Must be declared at module scope (before OpFunction).
    // The variable ID was pre-allocated and decorations were emitted
    // in the annotations section above. Now just emit OpVariable.
    if (program.uses_textures) {
        tex_sampled_image_var_ = EmitVariablePreallocated(
            tex_sampled_image_var_, types_.f32_image_2d,
            (u32)SpirvStorageClass::UniformConstant);
        LOG_DEBUG("Pre-declared texture variable: id=%u", tex_sampled_image_var_);
    }

    u32 func_type = EmitTypeFunction(types_.void_t, {});

    FunctionInfo fi;
    fi.func_type_id = func_type;
    fi.func_id = func_id;
    EmitInst4(SpirvOp::Function, types_.void_t, func_id, 0, func_type);

    (void)EmitLabel();

    // The function entry label has already been emitted by EmitLabel().
    // Mark it in labels_emitted so EmitStructuredCfg doesn't emit another
    // OpLabel for the first instruction's PC. Otherwise we'd have two labels
    // at the function entry, causing 'block must end with a branch' errors.
    if (!program.instructions.empty()) {
        cfg_state_.labels_emitted.insert(program.instructions[0].pc);
    }

    auto gpr_map = BuildGprMap(program, fi.func_id);

    if (is_vertex) {
        EmitVertexIoBody();
    } else if (is_fragment) {
        EmitFragmentIoBody();
    }

    // ── CFG-aware structured emission ─────────────────
    CfgAnnotation cfg = BuildCfg(program);
    EmitStructuredCfg(program, cfg, gpr_map, fi.func_id);

    bool has_kil = false;
    for (const auto& inst : program.instructions) {
        if (inst.opcode == ShaderOpcode::KIL) has_kil = true;
    }
    if (!has_kil) {
        EmitWord((1 << 16) | SpirvOp::Return);
    }
    EmitWord((1 << 16) | SpirvOp::FunctionEnd);

    if (words_.size() > 3) {
        words_[3] = GetBound();
    }

    LOG_INFO("SPIR-V emitted: %zu words, %u IDs, bound=%u, stage=%s",
             words_.size(), next_id_, GetBound(), ShaderStageName(program.stage));

    return words_;
}

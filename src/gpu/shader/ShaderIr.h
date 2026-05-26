#pragma once

#include "common/Types.h"
#include "common/Log.h"

#include <vector>
#include <string>
#include <array>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <unordered_set>

// ═══════════════════════════════════════════════════════════
// Shader Intermediate Representation (IR)
// ═══════════════════════════════════════════════════════════

// ── Shader stage ─────────────────────────────────────────────
enum class ShaderStage : u8 {
    VertexA     = 0,   // Vertex shader (first variant)
    VertexB     = 1,   // Vertex shader (second variant)
    TessControl = 2,   // Tessellation control / hull
    TessEval    = 3,   // Tessellation evaluation / domain
    Geometry    = 4,   // Geometry shader
    Fragment    = 5,   // Fragment / pixel shader
    Compute     = 6,   // Compute shader
};

constexpr const char* ShaderStageName(ShaderStage s) {
    switch (s) {
    case ShaderStage::VertexA:     return "VertexA";
    case ShaderStage::VertexB:     return "VertexB";
    case ShaderStage::TessControl: return "TessControl";
    case ShaderStage::TessEval:    return "TessEval";
    case ShaderStage::Geometry:    return "Geometry";
    case ShaderStage::Fragment:    return "Fragment";
    case ShaderStage::Compute:     return "Compute";
    }
    return "Unknown";
}

// ── Shader opcodes ───────────────────────────────────────────
// Covers the most common Maxwell ISA opcodes.
// Format: category + specific operation.
enum class ShaderOpcode : u16 {
    // Control flow
    NOP      = 0x0000,
    EXIT     = 0x0001,
    BRA      = 0x0002,   // Branch (conditional or unconditional)
    SSY      = 0x0003,   // Set synchronization point
    SYNC     = 0x0004,   // Wait for synchronization
    CALL     = 0x0005,   // Call subroutine
    RET      = 0x0006,   // Return from subroutine
    KIL      = 0x0007,   // Discard fragment
    CONT     = 0x0008,   // Continue (loop)
    BREAK    = 0x0009,   // Break (loop)

    // ALU: float
    FMAD     = 0x0100,   // Fused multiply-add: a * b + c
    FADD     = 0x0101,   // Float add
    FMUL     = 0x0102,   // Float multiply
    FMAX     = 0x0103,   // Float max
    FMIN     = 0x0104,   // Float min
    FSAT     = 0x0105,   // Float saturate
    FNEG     = 0x0106,   // Float negate
    FABS     = 0x0107,   // Float absolute
    FRCP     = 0x0108,   // Float reciprocal
    FRSQ     = 0x0109,   // Float reciprocal square root
    FSQRT    = 0x010A,   // Float square root
    FEX2     = 0x010B,   // Float exponent base 2
    FLG2     = 0x010C,   // Float log base 2
    FSIN     = 0x010D,   // Float sine (via MUFU)
    FCOS     = 0x010E,   // Float cosine (via MUFU)
    MUFU     = 0x010F,   // Multi-function unit (tan, floor, ceil, etc.)

    // ALU: integer
    IADD     = 0x0200,   // Integer add
    IMUL     = 0x0201,   // Integer multiply (low 32 bits)
    IMAD     = 0x0202,   // Integer multiply-add
    ISUB     = 0x0203,   // Integer subtract
    IABS     = 0x0204,   // Integer absolute
    INEG     = 0x0205,   // Integer negate
    ISETP    = 0x0206,   // Integer compare and set predicate
    I2F      = 0x0207,   // Integer to float
    I2I      = 0x0208,   // Integer type conversion
    IADD3    = 0x0209,   // Integer add with 3 inputs
    IMNMX    = 0x020A,   // Integer min/max
    LEA      = 0x020B,   // Load effective address
    POPC     = 0x020C,   // Population count
    FLO      = 0x020D,   // Find leading one
    SHL      = 0x020E,   // Shift left
    SHR      = 0x020F,   // Shift right

    // ALU: type conversion
    F2F      = 0x0300,   // Float to float (precision change)
    F2I      = 0x0301,   // Float to integer
    F2F_C    = 0x0302,   // Float to float with conversion

    // ALU: compare
    FSETP    = 0x0400,   // Float compare and set predicate
    FSET     = 0x0401,   // Float compare and set

    // Memory: load
    LD       = 0x0500,   // Load generic
    LDC      = 0x0501,   // Load from constant buffer
    LDS      = 0x0502,   // Load from shared memory
    LL       = 0x0503,   // Load local memory
    LG       = 0x0504,   // Load global memory
    LEA_LOAD = 0x0505,   // Load via LEA

    // Memory: store
    ST       = 0x0600,   // Store generic
    STS      = 0x0601,   // Store to shared memory
    STG      = 0x0602,   // Store to global memory
    STL      = 0x0603,   // Store to local memory

    // Texture
    TEX      = 0x0700,   // Texture sample
    TEXS     = 0x0701,   // Texture sample with separate sampler
    TLDS     = 0x0702,   // Texture load (non-sampled)
    TLD4     = 0x0703,   // Texture gather
    TXQ      = 0x0704,   // Texture query

    // Special
    S2R      = 0x0800,   // Special register read
    R2P      = 0x0801,   // Register to predicate
    P2R      = 0x0802,   // Predicate to register
    PRMT     = 0x0803,   // Permute register
    BFE      = 0x0804,   // Bit field extract
    BFI      = 0x0805,   // Bit field insert
    ATOM     = 0x0806,   // Atomic operation
    BAR      = 0x0807,   // Barrier
    DEPBAR   = 0x0808,   // Dependency barrier
    VOTE     = 0x0809,   // Vote (all/any)
    YIELD    = 0x080A,   // Yield thread
    MEMBAR   = 0x080B,   // Memory barrier
};

constexpr const char* ShaderOpcodeName(ShaderOpcode op) {
    switch (op) {
    case ShaderOpcode::NOP:    return "NOP";
    case ShaderOpcode::EXIT:   return "EXIT";
    case ShaderOpcode::BRA:    return "BRA";
    case ShaderOpcode::SSY:    return "SSY";
    case ShaderOpcode::SYNC:   return "SYNC";
    case ShaderOpcode::CALL:   return "CALL";
    case ShaderOpcode::RET:    return "RET";
    case ShaderOpcode::KIL:    return "KIL";
    case ShaderOpcode::CONT:   return "CONT";
    case ShaderOpcode::BREAK:  return "BREAK";
    case ShaderOpcode::FMAD:   return "FMAD";
    case ShaderOpcode::FADD:   return "FADD";
    case ShaderOpcode::FMUL:   return "FMUL";
    case ShaderOpcode::FMAX:   return "FMAX";
    case ShaderOpcode::FMIN:   return "FMIN";
    case ShaderOpcode::FSAT:   return "FSAT";
    case ShaderOpcode::FNEG:   return "FNEG";
    case ShaderOpcode::FABS:   return "FABS";
    case ShaderOpcode::FRCP:   return "FRCP";
    case ShaderOpcode::FRSQ:   return "FRSQ";
    case ShaderOpcode::FSQRT:  return "FSQRT";
    case ShaderOpcode::FEX2:   return "FEX2";
    case ShaderOpcode::FLG2:   return "FLG2";
    case ShaderOpcode::FSIN:   return "FSIN";
    case ShaderOpcode::FCOS:   return "FCOS";
    case ShaderOpcode::MUFU:   return "MUFU";
    case ShaderOpcode::IADD:   return "IADD";
    case ShaderOpcode::IMUL:   return "IMUL";
    case ShaderOpcode::IMAD:   return "IMAD";
    case ShaderOpcode::ISUB:   return "ISUB";
    case ShaderOpcode::IABS:   return "IABS";
    case ShaderOpcode::INEG:   return "INEG";
    case ShaderOpcode::ISETP:  return "ISETP";
    case ShaderOpcode::I2F:    return "I2F";
    case ShaderOpcode::I2I:    return "I2I";
    case ShaderOpcode::IADD3:  return "IADD3";
    case ShaderOpcode::IMNMX:  return "IMNMX";
    case ShaderOpcode::LEA:    return "LEA";
    case ShaderOpcode::POPC:   return "POPC";
    case ShaderOpcode::FLO:    return "FLO";
    case ShaderOpcode::SHL:    return "SHL";
    case ShaderOpcode::SHR:    return "SHR";
    case ShaderOpcode::F2F:    return "F2F";
    case ShaderOpcode::F2I:    return "F2I";
    case ShaderOpcode::F2F_C:  return "F2F_C";
    case ShaderOpcode::FSETP:  return "FSETP";
    case ShaderOpcode::FSET:   return "FSET";
    case ShaderOpcode::LD:     return "LD";
    case ShaderOpcode::LDC:    return "LDC";
    case ShaderOpcode::LDS:    return "LDS";
    case ShaderOpcode::LL:     return "LL";
    case ShaderOpcode::LG:     return "LG";
    case ShaderOpcode::LEA_LOAD: return "LEA_LOAD";
    case ShaderOpcode::ST:     return "ST";
    case ShaderOpcode::STS:    return "STS";
    case ShaderOpcode::STG:    return "STG";
    case ShaderOpcode::STL:    return "STL";
    case ShaderOpcode::TEX:    return "TEX";
    case ShaderOpcode::TEXS:   return "TEXS";
    case ShaderOpcode::TLDS:   return "TLDS";
    case ShaderOpcode::TLD4:   return "TLD4";
    case ShaderOpcode::TXQ:    return "TXQ";
    case ShaderOpcode::S2R:    return "S2R";
    case ShaderOpcode::R2P:    return "R2P";
    case ShaderOpcode::P2R:    return "P2R";
    case ShaderOpcode::PRMT:   return "PRMT";
    case ShaderOpcode::BFE:    return "BFE";
    case ShaderOpcode::BFI:    return "BFI";
    case ShaderOpcode::ATOM:   return "ATOM";
    case ShaderOpcode::BAR:    return "BAR";
    case ShaderOpcode::DEPBAR: return "DEPBAR";
    case ShaderOpcode::VOTE:   return "VOTE";
    case ShaderOpcode::YIELD:  return "YIELD";
    case ShaderOpcode::MEMBAR: return "MEMBAR";
    }
    return "UNKNOWN";
}

// ── Operand types ────────────────────────────────────────────
enum class OperandType : u8 {
    None,
    GPR,            // General purpose register (r0-r255)
    Predicate,      // Predicate register (p0-p7)
    FloatImm,       // Float immediate
    IntImm,         // Integer immediate
    BoolImm,        // Boolean immediate (0/1)
    Cbuf,           // Constant buffer access
    Attribute,      // Vertex attribute
    Varying,        // Interpolated varying (fragment input)
    SpecialReg,     // Special register (S2R)
    Address,        // Address register (a0-a3)
    Index,          // Index register
    Flag,           // Flag register (carry, overflow, etc.)
    Texture,        // Texture handle
    Sampler,        // Sampler handle
    Label,          // Branch target label
};

// ── Operand data type (bit width + interpretation) ──────────
enum class DataType : u8 {
    F32,            // 32-bit float
    F64,            // 64-bit float
    F16,            // 16-bit float
    U32,            // 32-bit unsigned integer
    S32,            // 32-bit signed integer
    U16,            // 16-bit unsigned integer
    S16,            // 16-bit signed integer
    U8,             // 8-bit unsigned integer
    S8,             // 8-bit signed integer
    B32,            // 32-bit bits
    B64,            // 64-bit bits
    B128,           // 128-bit bits (for texture)
};

// ── MUFU (Multi-Function Unit) operation ────────────────────
enum class MufuOp : u8 {
    COS    = 0,
    SIN    = 1,
    EX2    = 2,
    LG2    = 3,
    RCP    = 4,
    RSQ    = 5,
    SQRT   = 6,
    CBRT   = 7,
    TAN    = 8,
    FLOOR  = 9,
    CEIL   = 10,
};

// ── Memory space (for LD/ST) ────────────────────────────────
enum class MemorySpace : u8 {
    Generic    = 0,
    Global     = 1,
    Shared     = 2,
    Local      = 3,
    Constant   = 4,
    Uniform    = 5,
};

// ── Special registers (S2R) ─────────────────────────────────
enum class SpecialReg : u8 {
    LaneId        = 0,   // Thread lane ID
    ThreadIdX     = 1,   // Thread ID in X dimension
    ThreadIdY     = 2,
    ThreadIdZ     = 3,
    BlockSizeX    = 4,   // Block size in X
    BlockSizeY    = 5,
    BlockSizeZ    = 6,
    BlockIdX      = 7,   // Block ID in X
    BlockIdY      = 8,
    BlockIdZ      = 9,
    GridSizeX     = 10,  // Grid size in X
    GridSizeY     = 11,
    GridSizeZ     = 12,
    InvocationId  = 13,  // Invocation ID
    WrappedId     = 14,
    TessCoord     = 15,
    PrimitiveId   = 16,
};

// ── Condition code ──────────────────────────────────────────
enum class Condition : u8 {
    Always  = 0,       // PT (always true)
    Never   = 1,       // Never
    Eq      = 2,       // ==
    Ne      = 3,       // !=
    Lt      = 4,       // <
    Le      = 5,       // <=
    Gt      = 6,       // >
    Ge      = 7,       // >=
    // With negation
    NotEq   = 8,
    NotLt   = 9,
    NotLe   = 10,
    NotGt   = 11,
    NotGe   = 12,
};

// ── Operand ─────────────────────────────────────────────────
struct ShaderOperand {
    OperandType type = OperandType::None;
    DataType    data_type = DataType::F32;

    union {
        // GPR/Predicate/Address/Flag
        struct { u8 reg_index; } gpr;
        struct { u8 pred_index; bool negate; } pred;
        struct { u8 addr_index; } addr;

        // Immediate values
        f32  imm_f32;
        u32  imm_u32;
        s32  imm_s32;
        bool imm_bool;

        // Constant buffer: (cbuf_index, offset)
        struct { u32 cbuf_index; u32 offset; } cbuf;

        // Attribute: (attr_index, component)
        struct { u32 attr_index; u8 component; } attr;

        // Varying: (varying_index, component)
        struct { u32 varying_index; u8 component; } varying;

        // Special register
        SpecialReg special_reg;

        // Texture: (texture_index, sampler_index)
        struct { u32 tex_index; u32 sampler_index; } texture;

        // Label (branch target)
        u32 label;
    };

    // Modifiers
    bool negate   = false;
    bool absolute = false;
    bool saturate = false;   // Clamp to [0,1]

    // For MUFU operations
    MufuOp mufu_op = MufuOp::SIN;

    // For memory operations
    MemorySpace mem_space = MemorySpace::Generic;

    // ── Factory methods ───────────────────────────────
    static ShaderOperand Gpr(u8 index) {
        ShaderOperand op;
        op.type = OperandType::GPR;
        op.gpr.reg_index = index;
        return op;
    }

    static ShaderOperand Pred(u8 index, bool neg = false) {
        ShaderOperand op;
        op.type = OperandType::Predicate;
        op.pred.pred_index = index;
        op.pred.negate = neg;
        return op;
    }

    static ShaderOperand ImmF32(f32 val) {
        ShaderOperand op;
        op.type = OperandType::FloatImm;
        op.imm_f32 = val;
        op.data_type = DataType::F32;
        return op;
    }

    static ShaderOperand ImmS32(s32 val) {
        ShaderOperand op;
        op.type = OperandType::IntImm;
        op.imm_s32 = val;
        op.data_type = DataType::S32;
        return op;
    }

    static ShaderOperand ImmU32(u32 val) {
        ShaderOperand op;
        op.type = OperandType::IntImm;
        op.imm_u32 = val;
        op.data_type = DataType::U32;
        return op;
    }

    static ShaderOperand Cbuf(u32 index, u32 offset) {
        ShaderOperand op;
        op.type = OperandType::Cbuf;
        op.cbuf.cbuf_index = index;
        op.cbuf.offset = offset;
        return op;
    }

    static ShaderOperand Attr(u32 index, u8 comp = 0) {
        ShaderOperand op;
        op.type = OperandType::Attribute;
        op.attr.attr_index = index;
        op.attr.component = comp;
        return op;
    }

    static ShaderOperand Varying(u32 index, u8 comp = 0) {
        ShaderOperand op;
        op.type = OperandType::Varying;
        op.varying.varying_index = index;
        op.varying.component = comp;
        return op;
    }

    static ShaderOperand Special(SpecialReg reg) {
        ShaderOperand op;
        op.type = OperandType::SpecialReg;
        op.special_reg = reg;
        return op;
    }

    static ShaderOperand Texture(u32 tex_idx, u32 sampler_idx = 0) {
        ShaderOperand op;
        op.type = OperandType::Texture;
        op.texture.tex_index = tex_idx;
        op.texture.sampler_index = sampler_idx;
        return op;
    }

    static ShaderOperand Label(u32 lbl) {
        ShaderOperand op;
        op.type = OperandType::Label;
        op.label = lbl;
        return op;
    }

    // ── Debug ────────────────────────────────────────
    std::string ToString() const;
};

// ── Instruction ──────────────────────────────────────────────
struct ShaderInstruction {
    ShaderOpcode opcode = ShaderOpcode::NOP;
    ShaderOperand dest;
    ShaderOperand src[3];   // src0, src1, src2
    u16 src_count = 0;      // Number of source operands used
    u32 pc = 0;             // Program offset in bytes

    // Predicate guard
    // The instruction executes only when pred is true (or false if pred_negate)
    bool pred_guard = false;
    u8   pred_guard_index = 0;
    bool pred_guard_negate = false;

    // Conditional flags
    bool sets_cc = false;   // Sets condition codes
    bool is_delayed = false; // Has delay slot (for BRA/CALL etc.)

    // Memory info
    MemorySpace mem_space = MemorySpace::Generic;
    u32 mem_offset = 0;

    // Debug string
    std::string ToString() const;
};

// ── CFG block type (for SSY/SYNC structured control flow) ───
enum class CfgBlockType : u8 {
    Normal,         // Straight-line code
    Selection,      // If-else (SSY + forward conditional BRA → SYNC)
    Loop,           // Loop (SSY + backward conditional BRA → SYNC)
    Merge,          // Reconvergence point (SYNC target)
};

// ── SSY→SYNC pair annotation ───────────────────────────────
struct SsySyncPair {
    u32 ssy_pc = 0;        // PC of the SSY instruction
    u32 sync_pc = 0;       // PC of the matching SYNC instruction
    u32 bra_pc = 0;        // PC of the BRA that sits between them
    u32 bra_target_pc = 0; // Target PC of that BRA
    CfgBlockType type = CfgBlockType::Normal;
};

// ── CFG annotation (built during a pre-pass) ────────────────
struct CfgAnnotation {
    // Map from PC → instruction index in the program
    std::unordered_map<u32, u32> pc_to_index;

    // SSY→SYNC pairs found in the program
    std::vector<SsySyncPair> pairs;

    // Map from SYNC PC → SSY PC (for quick lookup during emission)
    std::unordered_map<u32, u32> sync_to_ssy;

    // Set of PCs that start a new basic block (branch targets, SYNC)
    std::unordered_set<u32> block_starts;

    // Clear all state
    void Clear() {
        pc_to_index.clear();
        pairs.clear();
        sync_to_ssy.clear();
        block_starts.clear();
    }
};

// ── Shader program ──────────────────────────────────────────
struct ShaderProgram {
    ShaderStage stage = ShaderStage::Fragment;
    std::vector<ShaderInstruction> instructions;
    u32 program_offset = 0;   // Offset in guest program region
    u32 program_size = 0;     // Size in bytes
    u64 hash = 0;             // Hash of the raw binary (for caching)

    // Metadata extracted during decoding
    u32 num_gprs_used = 0;    // Number of GPRs actually used
    u32 num_preds_used = 0;   // Number of predicates used
    bool uses_varyings = false;
    bool uses_textures = false;

    // Add an instruction
    void AddInst(const ShaderInstruction& inst) {
        instructions.push_back(inst);
    }

    // Debug dump
    void Dump() const;

    // Calculate a hash from the raw shader binary
    static u64 CalculateHash(const u8* data, u32 size);
};

// ═══════════════════════════════════════════════════════════
// SPIR-V related constants
// ═══════════════════════════════════════════════════════════

// SPIR-V magic number
constexpr u32 SPIRV_MAGIC = 0x07230203;
constexpr u32 SPIRV_VERSION = 0x00010000;  // SPIR-V 1.0

// SPIR-V storage classes
enum class SpirvStorageClass : u32 {
    UniformConstant = 0,
    Input           = 1,
    Uniform         = 2,
    Output          = 3,
    Workgroup       = 4,
    CrossWorkgroup  = 5,
    Private         = 6,
    Function        = 7,
    Generic         = 8,
    PushConstant    = 9,
    AtomicCounter   = 10,
    Image           = 11,
    StorageBuffer   = 12,
};

// SPIR-V decorations
enum class SpirvDecoration : u32 {
    Location           = 30,
    DescriptorSet      = 34,
    Binding            = 33,
    BuiltIn            = 11,
    Flat               = 13,
    NoPerspective      = 14,
    Centroid           = 17,
    Sample             = 18,
    RelaxedPrecision   = 35,
    ArrayStride        = 6,
};

// SPIR-V built-in values
enum class SpirvBuiltIn : u32 {
    Position           = 0,
    PointSize          = 1,
    ClipDistance       = 3,
    CullDistance       = 4,
    VertexId           = 5,
    InstanceId         = 6,
    PrimitiveId        = 7,
    InvocationId       = 8,
    Layer              = 9,
    ViewportIndex      = 10,
    TessLevelOuter     = 11,
    TessLevelInner     = 12,
    TessCoord          = 13,
    PatchVertices      = 14,
    FragCoord          = 15,
    PointCoord         = 16,
    FrontFacing        = 17,
    SampleId           = 18,
    SamplePosition     = 19,
    SampleMask         = 20,
    FragDepth          = 21,
    HelperInvocation   = 23,
    NumWorkgroups      = 24,
    WorkgroupSize      = 25,
    WorkgroupId        = 26,
    LocalInvocationId  = 27,
    GlobalInvocationId = 28,
    SubgroupSize       = 36,
    SubgroupInvocationId = 37,
    SubgroupEqMask     = 38,
    SubgroupGeMask     = 39,
    SubgroupGtMask     = 40,
    SubgroupLeMask     = 41,
    SubgroupLtMask     = 42,
    BaseVertex         = 44,
    BaseInstance       = 45,
    DrawIndex          = 46,
};


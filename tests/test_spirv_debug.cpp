#include <cstdio>
#include <vector>
#include "common/Log.h"
#include "common/Types.h"
#include "gpu/shader/SpirvEmitter.h"
#include "gpu/shader/ShaderIr.h"

int main() {
    Log::Init();
    Log::SetLevel(LogLevel::Error);

    // Minimal program: EXIT only
    ShaderProgram prog;
    prog.stage = ShaderStage::Fragment;
    ShaderInstruction inst;
    inst.opcode = ShaderOpcode::EXIT;
    inst.pc = 0;
    inst.src_count = 0;
    prog.AddInst(inst);

    SpirvEmitter emitter;
    auto spirv = emitter.Emit(prog);

    printf("SPIR-V (%zu words):\n", spirv.size());
    for (size_t i = 0; i < spirv.size(); i++) {
        u32 wc = spirv[i] >> 16;
        u32 op = spirv[i] & 0xFFFF;
        printf("  [%3zu] 0x%08X  wc=%u  op=%u", i, spirv[i], wc, op);
        if (op == 54) printf("  <-- Function!");
        if (op == 56) printf("  <-- FunctionEnd");
        if (op == 248) printf("  <-- Label/Branch");
        if (op == 253) printf("  <-- Return");
        if (op == 5) printf("  <-- Name");
        if (op == 11) printf("  <-- ExtInstImport");
        if (op == 14) printf("  <-- MemoryModel");
        if (op == 15) printf("  <-- EntryPoint");
        if (op == 16) printf("  <-- ExecutionMode");
        if (op == 17) printf("  <-- Capability");
        if (op == 43) printf("  <-- Constant");
        if (op == 59) printf("  <-- Variable");
        printf("\n");
    }
    return 0;
}

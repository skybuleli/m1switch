// ── Comprehensive test suite ─────────────────────────────────
// Tests all subsystems: Memory, CPU, GPU parser, IPC, Services.

#include "test_framework.h"
#include "common/Log.h"

#include "test_memory.cpp"
#include "test_cpu.cpp"
#include "test_gpu.cpp"
#include "test_ipc.cpp"
#include "test_maxwell_decoder.cpp"
#include "integration/test_shader_decode.cpp"
#include "test_spirv_emission.cpp"
#include "test_kernel.cpp"

int main(int argc, char** argv) {
    Log::Init();
    Log::SetLevel(LogLevel::Error);
    return RunAllTests();
}

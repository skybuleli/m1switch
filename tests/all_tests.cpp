// ── Comprehensive test suite ─────────────────────────────────
// Tests all subsystems: Memory, CPU, GPU parser, IPC, Services.

#include "test_framework.h"
#include "common/Log.h"

// Include all test modules
#include "test_memory.cpp"
#include "test_cpu.cpp"
#include "test_gpu.cpp"
#include "test_ipc.cpp"

int main(int argc, char** argv) {
    Log::Init();
    Log::SetLevel(LogLevel::Error);  // Quiet during tests
    return RunAllTests();
}

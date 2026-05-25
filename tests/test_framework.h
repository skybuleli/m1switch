// ── Minimal test framework (no external deps) ──────────────

#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <functional>
#include <string>
#include <vector>

class TestCase {
public:
    TestCase(const char* name, const char* file, int line,
             std::function<bool()> fn)
        : name_(name), file_(file), line_(line), fn_(std::move(fn)) {
        Register(this);
    }
    bool Run() const { return fn_(); }
    const char* Name() const { return name_.c_str(); }
    const char* File() const { return file_.c_str(); }
    int Line() const { return line_; }

    static void Register(TestCase* tc) {
        GetAll().push_back(tc);
    }
    static std::vector<TestCase*>& GetAll() {
        static std::vector<TestCase*> v;
        return v;
    }
private:
    std::string name_, file_;
    int line_;
    std::function<bool()> fn_;
};

static int g_passed = 0, g_failed = 0;

#define TEST(name) \
    static bool TEST_FN_##name(); \
    static TestCase TEST_REG_##name( \
        #name, __FILE__, __LINE__, TEST_FN_##name); \
    static bool TEST_FN_##name()

#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); return false; } \
} while(0)

#include "common/Types.h"
#define CHECK_EQ(a,b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a != _b) { printf("  FAIL %s:%d: (%llu != %llu)\n", __FILE__, __LINE__, (u64)_a, (u64)_b); return false; } \
} while(0)

int RunAllTests() {
    printf("=== Running %zu tests ===\n\n", TestCase::GetAll().size());
    for (auto* tc : TestCase::GetAll()) {
        printf("[ ] %s ... ", tc->Name());
        fflush(stdout);
        if (tc->Run()) { printf("PASS\n"); g_passed++; }
        else { printf("FAIL\n"); g_failed++; }
    }
    printf("\n=== %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}

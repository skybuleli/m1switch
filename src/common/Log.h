#pragma once

#include "Types.h"

#include <cstdio>
#include <cstring>
#include <string_view>
#include <mutex>
#include <thread>

// ── Minimal logging ──────────────────────────────────────────
// Phase 0: No dependency on spdlog yet.
// This minimal logger will be replaced when spdlog is integrated.

enum class LogLevel : u8 {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

class Log {
public:
    static void Init();
    static void Shutdown();

    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();

    static void Write(LogLevel level, const char* file, int line,
                      const char* func, const char* fmt, ...)
        __attribute__((format(printf, 5, 6)));

    // ── Log listener (for UI panel) ─────────────────────
    // Called for every log message after writing to stderr.
    using LogCallback = void(*)(LogLevel level, const char* msg, int len, void* user);
    static void SetCallback(LogCallback cb, void* user);

private:
    static std::mutex s_mutex;
    static LogLevel s_level;
    static LogCallback s_callback;
    static void* s_callback_user;

    static const char* LevelString(LogLevel level);
    static std::string_view Basename(std::string_view path);
};

// ── Convenience macros ───────────────────────────────────────
#define LOG_TRACE(...)  Log::Write(LogLevel::Trace, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_DEBUG(...)  Log::Write(LogLevel::Debug, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(...)   Log::Write(LogLevel::Info,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARN(...)   Log::Write(LogLevel::Warn,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(...)  Log::Write(LogLevel::Error, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_FATAL(...)  Log::Write(LogLevel::Fatal, __FILE__, __LINE__, __func__, __VA_ARGS__)

// ── Assertion ────────────────────────────────────────────────
#define ASSERT(cond, ...)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            Log::Write(LogLevel::Fatal, __FILE__, __LINE__, __func__,      \
                       "ASSERTION FAILED: " #cond);                        \
            Log::Write(LogLevel::Fatal, __FILE__, __LINE__, __func__,      \
                       __VA_ARGS__);                                       \
            __builtin_trap();                                              \
        }                                                                  \
    } while (0)

#define ASSERT_MSG(cond, msg) ASSERT(cond, "%s", msg)

// ── Unreachable marker ───────────────────────────────────────
#ifdef __APPLE__
#define UNREACHABLE() __builtin_trap()
#else
#define UNREACHABLE() __builtin_unreachable()
#endif

// ── Todo / Unimplemented ─────────────────────────────────────
#define TODO(msg) \
    Log::Write(LogLevel::Warn, __FILE__, __LINE__, __func__, "TODO: " msg)

#define UNIMPLEMENTED() \
    Log::Write(LogLevel::Warn, __FILE__, __LINE__, __func__, "UNIMPLEMENTED")

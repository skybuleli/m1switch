#include "Log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sys/time.h>

std::mutex Log::s_mutex;
LogLevel Log::s_level = LogLevel::Debug;

void Log::Init() {
    // Phase 0: simple stderr logging
    // Will add file output + spdlog later
    setvbuf(stderr, nullptr, _IOLBF, 1024);
    LOG_INFO("Logger initialized (level=%d)", static_cast<int>(s_level));
}

void Log::Shutdown() {
    LOG_INFO("Logger shutdown");
}

void Log::SetLevel(LogLevel level) {
    s_level = level;
}

LogLevel Log::GetLevel() {
    return s_level;
}

const char* Log::LevelString(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    default:              return "?????";
    }
}

std::string_view Log::Basename(std::string_view path) {
    auto pos = path.find_last_of('/');
    if (pos == std::string_view::npos) {
        pos = path.find_last_of('\\');
    }
    return pos != std::string_view::npos ? path.substr(pos + 1) : path;
}

void Log::Write(LogLevel level, const char* file, int line,
                const char* func, const char* fmt, ...) {
    if (static_cast<int>(level) < static_cast<int>(s_level)) {
        return;
    }

    // Format message
    char msg_buf[4096];
    va_list args;
    va_start(args, fmt);
    int msg_len = vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    if (msg_len < 0) {
        return; // encoding error
    }
    if (static_cast<size_t>(msg_len) >= sizeof(msg_buf)) {
        msg_len = sizeof(msg_buf) - 1;
    }

    // Timestamp
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);

    // Thread ID
    auto tid = pthread_mach_thread_np(pthread_self());

    // Write
    std::lock_guard<std::mutex> lock(s_mutex);
    fprintf(stderr, "%02d:%02d:%02d.%03d [%s] [0x%x] %s:%d:%s %.*s\n",
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            static_cast<int>(tv.tv_usec / 1000),
            LevelString(level),
            static_cast<unsigned>(tid),
            Basename(file).data(), line, func,
            msg_len, msg_buf);

    if (level == LogLevel::Fatal) {
        fprintf(stderr, "FATAL: aborting...\n");
        fflush(stderr);
        __builtin_trap();
    }
}

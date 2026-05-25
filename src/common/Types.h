#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>

// ── Fixed-width integer aliases ──────────────────────────────
using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using s8  = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

using f32 = float;
using f64 = double;

// ── GPU virtual address (40-bit on Tegra X1) ─────────────────
struct alignas(8) GpuVAddr {
    u64 addr : 40;
    u64 unused : 24;

    constexpr explicit GpuVAddr(u64 a) : addr(a), unused(0) {}
    constexpr GpuVAddr() : addr(0), unused(0) {}

    constexpr u64 value() const { return addr; }
    constexpr operator u64() const { return addr; }
    constexpr explicit operator bool() const { return addr != 0; }
};
static_assert(sizeof(GpuVAddr) == 8, "GpuVAddr must be 8 bytes");

// ── IO virtual address (for method params) ───────────────────
using Iova = GpuVAddr;

// ── Result type ──────────────────────────────────────────────
enum class Result : u32 {
    Success = 0,
    // General
    NotImplemented = 0x1001,
    InvalidArgument = 0x1002,
    OutOfMemory = 0x1003,
    NotFound = 0x1004,
    PermissionDenied = 0x1005,
    // Kernel
    InvalidHandle = 0x2001,
    TimedOut = 0x2002,
    ThreadTerminating = 0x2003,
    // FS
    FsFileNotFound = 0x3001,
    FsInvalidNCA = 0x3002,
    FsIntegrityCheckFailed = 0x3003,
    // GPU
    GpuMethodNotFound = 0x4001,
    GpuShaderCompileFailed = 0x4002,
    GpuOutOfMemory = 0x4003,
};

inline bool Failed(Result r) { return static_cast<u32>(r) != 0; }

// ── Alignment helpers ────────────────────────────────────────
template <typename T>
constexpr T AlignUp(T value, T alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

template <typename T>
constexpr T AlignDown(T value, T alignment) {
    return value & ~(alignment - 1);
}

template <typename T>
constexpr bool IsAligned(T value, T alignment) {
    return (value & (alignment - 1)) == 0;
}

// ── Bit manipulation ─────────────────────────────────────────
template <typename T>
constexpr T Bits(T value, int hi, int lo) {
    return (value >> lo) & ((T(1) << (hi - lo + 1)) - 1);
}

template <typename T>
constexpr bool Bit(T value, int bit) {
    return (value >> bit) & 1;
}

// ── Non-copyable / Non-movable base ──────────────────────────
class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
};

// ── Enum bitwise ops ─────────────────────────────────────────
template <typename E>
constexpr std::underlying_type_t<E> ToUnderlying(E e) {
    return static_cast<std::underlying_type_t<E>>(e);
}

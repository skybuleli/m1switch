#pragma once

#include "common/Types.h"
#include "common/Log.h"

#include <mach/mach.h>
#include <span>
#include <unordered_map>
#include <mutex>
#include <vector>

// ── Forward declarations ────────────────────────────────────
struct MemoryPage;

// ── Guest physical memory manager ───────────────────────────
// Allocates a contiguous 4 GiB address space via mach_vm_allocate.
// Tracks sub-allocations (code segments, heap, stack, etc.)
// at 4 KiB page granularity.
//
// On M1 with UMA, the base address can also be wrapped as an
// MTLBuffer for zero-copy GPU access.

class Memory {
public:
    static constexpr u64 ADDR_SPACE_SIZE = 1ULL << 32;  // 4 GiB
    static constexpr u64 PAGE_SIZE      = 0x1000;       // 4 KiB
    static constexpr u64 PAGE_MASK      = PAGE_SIZE - 1;
    static constexpr u64 HEAP_BASE      = 0x80000000;
    static constexpr u64 STACK_BASE     = 0xC0000000;
    static constexpr u64 CODE_BASE      = 0x40000000;

    enum class Permission : u8 {
        None  = 0,
        R     = 1,
        W     = 2,
        RW    = 3,
        X     = 4,
        RX    = 5,
        RWX   = 7,
    };

    Memory();
    ~Memory();

    // ── Allocation (called by loader) ────────────────────
    Result MapPhysical(u64 address, size_t size, Permission perm,
                       const void* data = nullptr);
    Result UnmapPhysical(u64 address, size_t size);

    // ── Heap management (called by svcSetHeapSize) ───────
    Result SetHeapSize(u64 size);
    u64    GetHeapSize() const { return heap_size_; }

    // ── Stack (called by loader / svc) ───────────────────
    Result SetupStack(u64 size);
    u64    GetStackTop() const { return stack_top_; }

    // ── Guest memory access ──────────────────────────────
    template <typename T>
    Result Read(u64 address, T* value) const;

    template <typename T>
    Result Write(u64 address, T value);

    [[nodiscard]] u8* Pointer(u64 address) const;
    [[nodiscard]] bool IsValid(u64 address) const;

    // ── GPU UMA ──────────────────────────────────────────
    // Returns the base pointer, usable for MTLBuffer creation
    [[nodiscard]] void* BasePointer() const { return base_; }
    [[nodiscard]] mach_vm_address_t BaseAddress() const { return base_addr_; }

    // ── Debug ────────────────────────────────────────────
    void DumpPages() const;

private:
    Result AllocateSpace();
    void  FreeSpace();

    mach_vm_address_t base_addr_ = 0;  // 4GiB aligned base
    void*             base_      = nullptr;

    // Heap tracking
    u64 heap_size_ = 0;
    u64 heap_addr_ = HEAP_BASE;

    // Stack
    u64 stack_size_ = 0;
    u64 stack_top_  = 0;

    // Page tracking (for svcQueryMemory)
    struct PageInfo {
        u64 size;
        u32 flags;  // Permission + type flags
    };
    std::unordered_map<u64, PageInfo> pages_;
    mutable std::mutex mutex_;
};

// ── Permission conversion ───────────────────────────────────
int MachVmProtFromPerm(Memory::Permission perm);
Memory::Permission PermFromMachVmProt(int prot);

// ── Template implementations (included for inline) ──────────

template <typename T>
Result Memory::Read(u64 address, T* value) const {
    if (!IsValid(address) || (address + sizeof(T)) > ADDR_SPACE_SIZE) {
        return Result::InvalidArgument;
    }
    auto* base = static_cast<const u8*>(base_);
    std::memcpy(value, base + address, sizeof(T));
    return Result::Success;
}

template <typename T>
Result Memory::Write(u64 address, T value) {
    if (!IsValid(address) || (address + sizeof(T)) > ADDR_SPACE_SIZE) {
        return Result::InvalidArgument;
    }
    auto* base = static_cast<u8*>(base_);
    std::memcpy(base + address, &value, sizeof(T));
    return Result::Success;
}

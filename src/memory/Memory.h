#pragma once

#include "common/Types.h"
#include "common/Log.h"

#include <mach/mach.h>
#include <span>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <cstring>

struct MemoryPage;

class Memory {
public:
    static constexpr u64 ADDR_SPACE_SIZE = 1ULL << 32;
    static constexpr u64 PAGE_SIZE      = 0x1000;
    static constexpr u64 PAGE_MASK      = PAGE_SIZE - 1;
    static constexpr u64 HEAP_BASE      = 0x80000000;
    static constexpr u64 STACK_BASE     = 0xC0000000;
    static constexpr u64 CODE_BASE      = 0x40000000;

    enum class Permission : u8 {
        None = 0, R = 1, W = 2, RW = 3, X = 4, RX = 5, RWX = 7,
    };

    Memory();
    ~Memory();

    Result MapPhysical(u64 address, size_t size, Permission perm,
                       const void* data = nullptr);
    Result UnmapPhysical(u64 address, size_t size);
    Result Protect(u64 address, size_t size, Permission perm);

    Result SetHeapSize(u64 size);
    u64    GetHeapSize() const { return heap_size_; }
    u64    GetHeapBase() const { return heap_addr_; }

    Result SetupStack(u64 size);
    u64    GetStackTop() const { return stack_top_; }

    template <typename T>
    Result Read(u64 address, T* value) const;

    template <typename T>
    Result Write(u64 address, T value);

    [[nodiscard]] u8* Pointer(u64 address) const;
    [[nodiscard]] bool IsValid(u64 address) const;
    [[nodiscard]] void* BasePointer() const { return base_; }
    [[nodiscard]] mach_vm_address_t BaseAddress() const { return base_addr_; }

    void DumpPages() const;

private:
    Result AllocateSpace();
    void  FreeSpace();

    mach_vm_address_t base_addr_ = 0;
    void*             base_      = nullptr;
    u64 heap_size_ = 0;
    u64 heap_addr_ = HEAP_BASE;
    u64 stack_size_ = 0;
    u64 stack_top_  = 0;

    struct PageInfo { u64 size; u32 flags; };
    std::unordered_map<u64, PageInfo> pages_;
    mutable std::mutex mutex_;
};

template <typename T>
Result Memory::Read(u64 address, T* value) const {
    if (!IsValid(address) || (address + sizeof(T)) > ADDR_SPACE_SIZE)
        return Result::InvalidArgument;
    auto* base = static_cast<const u8*>(base_);
    std::memcpy(value, base + address, sizeof(T));
    return Result::Success;
}

template <typename T>
Result Memory::Write(u64 address, T value) {
    if (!IsValid(address) || (address + sizeof(T)) > ADDR_SPACE_SIZE)
        return Result::InvalidArgument;
    auto* base = static_cast<u8*>(base_);
    std::memcpy(base + address, &value, sizeof(T));
    return Result::Success;
}

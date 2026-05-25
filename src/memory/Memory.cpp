#include "memory/Memory.h"
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <cstring>
#include <algorithm>

// ── Permission conversion ───────────────────────────────────
int mach_vm_prot_from_perm(Memory::Permission perm) {
    using P = Memory::Permission;
    int prot = VM_PROT_NONE;
    switch (perm) {
    case P::R:   prot = VM_PROT_READ; break;
    case P::W:   prot = VM_PROT_WRITE; break;
    case P::RW:  prot = VM_PROT_READ | VM_PROT_WRITE; break;
    case P::X:   prot = VM_PROT_EXECUTE; break;
    case P::RX:  prot = VM_PROT_READ | VM_PROT_EXECUTE; break;
    case P::RWX: prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE; break;
    default: break;
    }
    return prot;
}

Memory::Memory() {
    // Note: we do NOT pre-allocate the full 4GB.
    // Pages are allocated on demand in MapPhysical.
    // base_addr_ is set to a reasonable starting point (reserved by convention).
    base_addr_ = 0x300000000ULL;  // fixed base for reproducibility
    base_ = reinterpret_cast<void*>(base_addr_);
    LOG_INFO("Guest memory base: 0x%llx (4 GiB virtual address space)", base_addr_);
}

Memory::~Memory() {
    // Free all tracked pages
    for (auto& [addr, info] : pages_) {
        mach_vm_deallocate(mach_task_self(), base_addr_ + addr, info.size);
    }
}

// ── Map physical memory ─────────────────────────────────────
Result Memory::MapPhysical(u64 address, size_t size, Permission perm,
                            const void* data) {
    if (!IsAligned(static_cast<u64>(address), PAGE_SIZE) ||
        !IsAligned(static_cast<u64>(size), PAGE_SIZE)) {
        return Result::InvalidArgument;
    }
    if (address + size > ADDR_SPACE_SIZE)
        return Result::InvalidArgument;

    mach_vm_address_t abs_addr = base_addr_ + address;
    int prot = mach_vm_prot_from_perm(perm);

    // Allocate memory at the absolute address
    // VM_FLAGS_OVERWRITE lets us replace any existing mapping
    kern_return_t kr = mach_vm_allocate(
        mach_task_self(),
        &abs_addr,
        size,
        VM_FLAGS_FIXED
    );

    if (kr == KERN_NO_SPACE) {
        // Address range already allocated — free and retry
        mach_vm_deallocate(mach_task_self(), abs_addr, size);
        kr = mach_vm_allocate(
            mach_task_self(),
            &abs_addr,
            size,
            VM_FLAGS_FIXED
        );
    }

    if (kr != KERN_SUCCESS) {
        LOG_ERROR("MapPhysical(0x%llx, %zu): mach_vm_allocate failed: %d",
                  address, size, kr);
        return Result::OutOfMemory;
    }

    // Copy initial data while memory is still writable
    if (data) {
        std::memcpy(reinterpret_cast<void*>(abs_addr), data, size);
    }

    // Set protection
    mach_vm_protect(mach_task_self(), abs_addr, size, false, prot);

    // Track
    std::lock_guard<std::mutex> lock(mutex_);
    pages_[address] = {size, static_cast<u32>(perm)};

    LOG_TRACE("MapPhysical(0x%llx, %zu, perm=%d)", address, size, (int)perm);
    return Result::Success;
}

Result Memory::UnmapPhysical(u64 address, size_t size) {
    if (!IsAligned(static_cast<u64>(address), PAGE_SIZE))
        return Result::InvalidArgument;

    mach_vm_address_t abs_addr = base_addr_ + address;
    kern_return_t kr = mach_vm_deallocate(mach_task_self(), abs_addr, size);
    if (kr != KERN_SUCCESS)
        return Result::InvalidArgument;

    std::lock_guard<std::mutex> lock(mutex_);
    pages_.erase(address);
    return Result::Success;
}

// ── Heap ────────────────────────────────────────────────────
Result Memory::SetHeapSize(u64 size) {
    u64 aligned = AlignUp(static_cast<u64>(size), PAGE_SIZE);
    if (aligned > 0x40000000) return Result::OutOfMemory;

    if (aligned > heap_size_) {
        Result r = MapPhysical(HEAP_BASE, aligned, Permission::RW);
        if (Failed(r)) return r;
    } else if (aligned < heap_size_) {
        UnmapPhysical(heap_addr_ + aligned, heap_size_ - aligned);
    }
    heap_addr_ = HEAP_BASE;
    heap_size_ = aligned;
    return Result::Success;
}

Result Memory::SetupStack(u64 size) {
    u64 aligned = std::min(AlignUp(static_cast<u64>(size), PAGE_SIZE), 0x100000ULL);
    u64 stack_addr = STACK_BASE - aligned;
    Result r = MapPhysical(stack_addr, aligned, Permission::RW);
    if (Failed(r)) return r;
    stack_size_ = aligned;
    stack_top_ = STACK_BASE;
    return Result::Success;
}

// ── Accessors ───────────────────────────────────────────────
u8* Memory::Pointer(u64 address) const {
    if (!IsValid(address)) return nullptr;
    return reinterpret_cast<u8*>(base_addr_ + address);
}

bool Memory::IsValid(u64 address) const {
    return address < ADDR_SPACE_SIZE;
}

void Memory::DumpPages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("--- Memory Pages (%zu entries) ---", pages_.size());
    for (auto& [addr, info] : pages_)
        LOG_INFO("  0x%08llx: %llu bytes, flags=0x%x", addr, info.size, info.flags);
}

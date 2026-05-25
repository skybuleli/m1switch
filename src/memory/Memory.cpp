#include "memory/Memory.h"

#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <cstring>

// ── Utility: permission conversion ──────────────────────────
int mach_vm_prot_from_perm(Memory::Permission perm) {
    int prot = VM_PROT_NONE;
    switch (perm) {
    case Memory::Permission::R:   prot = VM_PROT_READ; break;
    case Memory::Permission::W:   prot = VM_PROT_WRITE; break;
    case Memory::Permission::RW:  prot = VM_PROT_READ | VM_PROT_WRITE; break;
    case Memory::Permission::X:   prot = VM_PROT_EXECUTE; break;
    case Memory::Permission::RX:  prot = VM_PROT_READ | VM_PROT_EXECUTE; break;
    case Memory::Permission::RWX: prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE; break;
    default: break;
    }
    return prot;
}

Memory::Permission perm_from_mach_vm_prot(int prot) {
    using P = Memory::Permission;
    bool r = prot & VM_PROT_READ;
    bool w = prot & VM_PROT_WRITE;
    bool x = prot & VM_PROT_EXECUTE;
    if (r && w && x) return P::RWX;
    if (r && x)      return P::RX;
    if (r && w)      return P::RW;
    if (w)           return P::W;
    if (x)           return P::X;
    if (r)           return P::R;
    return P::None;
}

// ── Construction / Destruction ──────────────────────────────
Memory::Memory() {
    Result r = AllocateSpace();
    if (Failed(r)) {
        LOG_FATAL("Failed to allocate 4GB guest address space");
    }
    LOG_INFO("Guest memory allocated: 4 GiB @ 0x%llx", base_addr_);
}

Memory::~Memory() {
    FreeSpace();
}

// ── Allocate 4 GiB address space ────────────────────────────
Result Memory::AllocateSpace() {
    // Reserve 4 GiB of contiguous virtual address space.
    // We use VM_FLAGS_ANYWHERE since we don't care where it goes.
    kern_return_t kr = mach_vm_allocate(
        mach_task_self(),
        &base_addr_,
        ADDR_SPACE_SIZE,
        VM_FLAGS_ANYWHERE
    );

    if (kr != KERN_SUCCESS) {
        LOG_ERROR("mach_vm_allocate 4GiB failed: %d (0x%x)", kr, kr);
        return Result::OutOfMemory;
    }

    base_ = reinterpret_cast<void*>(base_addr_);

    // Mark as "reserved" in page table
    std::lock_guard<std::mutex> lock(mutex_);
    pages_[0] = {ADDR_SPACE_SIZE, 0};  // Whole space reserved

    return Result::Success;
}

void Memory::FreeSpace() {
    if (base_addr_ != 0) {
        mach_vm_deallocate(mach_task_self(), base_addr_, ADDR_SPACE_SIZE);
        base_addr_ = 0;
        base_ = nullptr;
        LOG_INFO("Guest memory freed");
    }
}

// ── Map physical memory ─────────────────────────────────────
Result Memory::MapPhysical(u64 address, size_t size, Permission perm,
                            const void* data) {
    if (!IsAligned(static_cast<u64>(address), PAGE_SIZE) ||
        !IsAligned(static_cast<u64>(size), PAGE_SIZE)) {
        return Result::InvalidArgument;
    }
    if (address + size > ADDR_SPACE_SIZE) {
        return Result::InvalidArgument;
    }

    int prot = mach_vm_prot_from_perm(perm);

    vm_prot_t cur_prot = prot;
    vm_prot_t max_prot = prot | VM_PROT_READ;  // always allow reading max

    // Convert guest address to host absolute address
    mach_vm_address_t abs_addr = base_addr_ + address;

    kern_return_t kr = mach_vm_allocate(
        mach_task_self(),
        &abs_addr,
        size,
        VM_FLAGS_FIXED
    );

    if (kr == KERN_NO_SPACE) {
        // Already mapped — try remap (overwrite)
        mach_vm_address_t remap_addr = base_addr_ + address;
        kr = mach_vm_remap(
            mach_task_self(),
            &remap_addr,
            size,
            0,
            VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
            mach_task_self(),
            base_addr_ + address,
            false,
            &cur_prot,
            &max_prot,
            VM_INHERIT_NONE
        );
    }

    if (kr != KERN_SUCCESS) {
        LOG_ERROR("MapPhysical(0x%llx, %zu) failed: %d", address, size, kr);
        return Result::OutOfMemory;
    }

    // Set final protection
    mach_vm_protect(mach_task_self(), base_addr_ + address, size, false, cur_prot);

    // Copy data if provided
    if (data) {
        auto* dst = static_cast<char*>(base_) + address;
        std::memcpy(dst, data, size);
    }

    // Track in page table
    std::lock_guard<std::mutex> lock(mutex_);
    pages_[address] = {size, static_cast<u32>(perm)};

    LOG_TRACE("MapPhysical(0x%llx, %zu, perm=%d)", address, size, (int)perm);
    return Result::Success;
}

Result Memory::UnmapPhysical(u64 address, size_t size) {
    if (!IsAligned(address, PAGE_SIZE)) {
        return Result::InvalidArgument;
    }
    kern_return_t kr = mach_vm_deallocate(mach_task_self(), base_addr_ + address, size);
    if (kr != KERN_SUCCESS) {
        LOG_ERROR("UnmapPhysical(0x%llx, %zu) failed: %d", address, size, kr);
        return Result::InvalidArgument;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pages_.erase(address);
    return Result::Success;
}

// ── Heap ────────────────────────────────────────────────────
Result Memory::SetHeapSize(u64 size) {
    if (size == 0) {
        // Free heap
        if (heap_size_ > 0) {
            UnmapPhysical(heap_addr_, heap_size_);
            heap_size_ = 0;
        }
        return Result::Success;
    }

    if (size > 0x40000000) {  // 1 GiB max heap
        return Result::OutOfMemory;
    }

    u64 aligned_size = AlignUp(size, PAGE_SIZE);

    if (aligned_size > heap_size_) {
        // Grow: allocate new heap, copy old data
        // For Phase 1, simple: allocate at HEAP_BASE
        Result r = MapPhysical(HEAP_BASE, aligned_size, Permission::RW);
        if (Failed(r)) return r;
    } else if (aligned_size < heap_size_) {
        // Shrink: unmap excess pages
        UnmapPhysical(heap_addr_ + aligned_size, heap_size_ - aligned_size);
    }

    heap_addr_ = HEAP_BASE;
    heap_size_ = aligned_size;
    return Result::Success;
}

// ── Stack ───────────────────────────────────────────────────
Result Memory::SetupStack(u64 size) {
    u64 aligned_size = AlignUp(size, PAGE_SIZE);
    if (aligned_size > 0x100000) {  // 1 MiB max stack
        aligned_size = 0x100000;
    }

    // Stack grows downward — top of allocated region
    u64 stack_addr = STACK_BASE - aligned_size;

    Result r = MapPhysical(stack_addr, aligned_size, Permission::RW);
    if (Failed(r)) return r;

    stack_size_ = aligned_size;
    stack_top_ = STACK_BASE;  // SP starts at top of reserved region
    return Result::Success;
}

// ── Accessors ───────────────────────────────────────────────
u8* Memory::Pointer(u64 address) const {
    if (!IsValid(address)) return nullptr;
    return reinterpret_cast<u8*>(base_addr_ + address);
}

bool Memory::IsValid(u64 address) const {
    // Check against known allocations
    // Phase 1: simple bound check
    return address < ADDR_SPACE_SIZE;
}

void Memory::DumpPages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("--- Memory Pages (%zu entries) ---", pages_.size());
    for (auto& [addr, info] : pages_) {
        LOG_INFO("  0x%08llx: %llu bytes, flags=0x%x",
                 addr, info.size, info.flags);
    }
}

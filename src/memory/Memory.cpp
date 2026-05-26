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

Result Memory::Protect(u64 address, size_t size, Permission perm) {
    mach_vm_address_t abs_addr = base_addr_ + address;
    int prot = mach_vm_prot_from_perm(perm);
    kern_return_t kr = mach_vm_protect(mach_task_self(), abs_addr, size, false, prot);
    if (kr != KERN_SUCCESS)
        return Result::InvalidArgument;
    return Result::Success;
}

// ── Heap (supports multiple resizes) ─────────────────────────
Result Memory::SetHeapSize(u64 size) {
    u64 aligned = AlignUp(static_cast<u64>(size), PAGE_SIZE);
    if (aligned > 0x40000000) return Result::OutOfMemory;
    if (aligned == heap_size_) return Result::Success;

    if (aligned > heap_size_) {
        // Growing: unmap old first to avoid overlap, then map new
        if (heap_size_ > 0)
            UnmapPhysical(HEAP_BASE, heap_size_);
        Result r = MapPhysical(HEAP_BASE, aligned, Permission::RW);
        if (Failed(r)) {
            // Restore old size on failure
            if (heap_size_ > 0)
                MapPhysical(HEAP_BASE, heap_size_, Permission::RW);
            return r;
        }
    } else {
        // Shrinking: unmap the excess
        UnmapPhysical(HEAP_BASE + aligned, heap_size_ - aligned);
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

void Memory::QueryRegion(u64 address, u64& out_base, u64& out_size,
                          u32& out_type) const {
    out_base = address;
    out_size = 0x1000;
    out_type = 3; // MemType_Unmapped

    std::lock_guard<std::mutex> lock(mutex_);

    // Find the page containing this address (linear scan)
    // unordered_map doesn't support upper_bound, so we iterate
    typename std::unordered_map<u64, PageInfo>::const_iterator it = pages_.end();
    for (auto p = pages_.begin(); p != pages_.end(); ++p) {
        u64 pg_base = p->first;
        u64 pg_end = pg_base + p->second.size;
        if (address >= pg_base && address < pg_end) {
            it = p;
            break;
        }
    }

    if (it != pages_.end()) {
        u64 page_base = it->first;
        u64 page_end = page_base + it->second.size;

        if (address >= page_base && address < page_end) {
            out_base = page_base;
            out_size = it->second.size;
            u32 flags = it->second.flags;

            // Map flags to type:
            // 0=Code (RX), 1=RW data, 2=RO data, 3=Unmapped, 4=Heap, 5=Stack
            if (flags & (u32)Memory::Permission::X) {
                out_type = 0; // Code
            } else if (flags & (u32)Memory::Permission::W) {
                out_type = 1; // RW data
            } else if (flags & (u32)Memory::Permission::R) {
                out_type = 2; // RO data
            }

            // Override for known special regions
            if (page_base >= HEAP_BASE && page_base < HEAP_BASE + 0x40000000)
                out_type = 4; // MemType_Heap
            if (page_base >= STACK_BASE - 0x1000000 && page_base < STACK_BASE)
                out_type = 5; // MemType_Stack
        }
    }
}

void Memory::DumpPages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("--- Memory Pages (%zu entries) ---", pages_.size());
    for (auto& [addr, info] : pages_)
        LOG_INFO("  0x%08llx: %llu bytes, flags=0x%x", addr, info.size, info.flags);
}

// ── C API for debug panels ──────────────────────────────────
extern "C" void Memory_DumpPages() {
    // Can't access a specific Memory instance from here,
    // so this is a no-op until we track it globally.
    LOG_INFO("Memory_DumpPages: call via Memory::DumpPages on active instance");
}

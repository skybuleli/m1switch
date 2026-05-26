#include "cpu/Debugger.h"
#include "cpu/ExceptionHandler.h"
#include "common/Log.h"

#include <cstring>
#include <libkern/OSCacheControl.h>
#include <mach/mach_vm.h>

// ── 全局单例 ────────────────────────────────────────────
static EmuDebugger s_global_debugger;

EmuDebugger& GlobalDebugger() {
    return s_global_debugger;
}


EmuDebugger::EmuDebugger() = default;

// ── BRK 指令构造 ─────────────────────────────────────
u32 EmuDebugger::MakeBrkDebug(u64 addr) {
    // BRK #imm16: 0xD4200000 | (imm16 << 5)
    // 使用 tag = BRK_TAG_DEBUG | (addr & 0xFFF) 作为标识
    u32 tag = BRK_TAG_DEBUG | (static_cast<u32>(addr) & 0xFFF);
    return 0xD4200000 | (tag << 5);
}

// 辅助：主机绝对地址 → 客户相对地址
static u64 ToGuestRel(Memory* mem, u64 host_addr) {
    return mem ? host_addr - mem->BaseAddress() : host_addr;
}

// 辅助：临时将目标页改为 RW（写入 BRK 的准备工作）
// 目标页的 max_prot 已在 Memory::MapPhysical 中设为 RWX，因此 prot 降级/升级可行
static void MakePageWritable(u64 host_addr) {
    u64 page_start = host_addr & ~(u64)0xFFF;
    mach_vm_protect(mach_task_self(), page_start, 0x1000, FALSE,
                    VM_PROT_READ | VM_PROT_WRITE);
}

// 辅助：将目标页恢复为 RX
static void MakePageExecutable(u64 host_addr) {
    u64 page_start = host_addr & ~(u64)0xFFF;
    mach_vm_protect(mach_task_self(), page_start, 0x1000, FALSE,
                    VM_PROT_READ | VM_PROT_EXECUTE);
}

Result EmuDebugger::PatchBreakpoint(u64 addr, bool set) {
    if (!memory_) return Result::InvalidArgument;
    MakePageWritable(addr);
    auto* ptr = memory_->Pointer(ToGuestRel(memory_, addr));
    if (!ptr) { MakePageExecutable(addr); return Result::InvalidArgument; }

    if (set) {
        u32 brk = MakeBrkDebug(addr);
        std::memcpy(ptr, &brk, sizeof(brk));
        // 刷新 icache
        sys_icache_invalidate(ptr, sizeof(brk));
    }
    MakePageExecutable(addr);
    // 恢复：不做操作，由 ApplyBreakpoints/RestoreBreakpoints 处理
    return Result::Success;
}

// ── 批量应用/恢复断点 ────────────────────────────────
Result EmuDebugger::ApplyBreakpoints() {
    std::lock_guard<std::mutex> lock(breakpoint_mutex_);
    for (auto& [addr, bp] : breakpoints_) {
        if (bp.enabled && !bp.patched) {
            MakePageWritable(addr);
            auto* ptr = memory_ ? memory_->Pointer(ToGuestRel(memory_, addr)) : nullptr;
            if (ptr) {
                std::memcpy(&bp.original_inst, ptr, sizeof(bp.original_inst));
                u32 brk = MakeBrkDebug(addr);
                std::memcpy(ptr, &brk, sizeof(brk));
                sys_icache_invalidate(ptr, sizeof(brk));
                bp.patched = true;
            }
            MakePageExecutable(addr);
        }
    }
    return Result::Success;
}

Result EmuDebugger::RestoreBreakpoints() {
    std::lock_guard<std::mutex> lock(breakpoint_mutex_);
    for (auto& [addr, bp] : breakpoints_) {
        if (bp.patched && bp.original_inst != 0) {
            MakePageWritable(addr);
            auto* ptr = memory_ ? memory_->Pointer(ToGuestRel(memory_, addr)) : nullptr;
            if (ptr) {
                std::memcpy(ptr, &bp.original_inst, sizeof(bp.original_inst));
                sys_icache_invalidate(ptr, sizeof(bp.original_inst));
                bp.patched = false;
            }
            MakePageExecutable(addr);
        }
    }
    return Result::Success;
}

// ── 断点管理 ─────────────────────────────────────────
void EmuDebugger::SetBreakpoint(u64 host_addr) {
    if (host_addr == 0) return;
    std::lock_guard<std::mutex> lock(breakpoint_mutex_);
    auto [it, inserted] = breakpoints_.emplace(host_addr, Breakpoint{});
    if (!inserted) {
        it->second.enabled = true;
        LOG_DEBUG("Debugger: breakpoint re-enabled at 0x%llx", host_addr);
        return;
    }

    // 立即打补丁（如果 memory 已可用）
    // 注意：memory_->Pointer() 使用客户相对地址，而 host_addr 是主机绝对地址
    // 访存页当前可能为 RX（加载器保护后），需临时降级为 RW
    if (memory_) {
        u64 guest_rel = host_addr - memory_->BaseAddress();
        MakePageWritable(host_addr);
        auto* ptr = memory_->Pointer(guest_rel);
        if (ptr) {
            std::memcpy(&it->second.original_inst, ptr, sizeof(u32));
            u32 brk = MakeBrkDebug(host_addr);
            std::memcpy(ptr, &brk, sizeof(brk));
            sys_icache_invalidate(ptr, sizeof(brk));
            it->second.patched = true;
        }
        MakePageExecutable(host_addr);
        if (ptr) {
            LOG_DEBUG("Debugger: breakpoint set at 0x%llx (patched)", host_addr);
            return;
        }
    }
    LOG_DEBUG("Debugger: breakpoint queued at 0x%llx (no memory)", host_addr);
}

void EmuDebugger::RemoveBreakpoint(u64 host_addr) {
    std::lock_guard<std::mutex> lock(breakpoint_mutex_);
    auto it = breakpoints_.find(host_addr);
    if (it == breakpoints_.end()) return;

    // 恢复原始指令（页可能为 RX，需临时降级）
    if (it->second.patched && memory_) {
        MakePageWritable(host_addr);
        auto* ptr = memory_->Pointer(ToGuestRel(memory_, host_addr));
        if (ptr && it->second.original_inst != 0) {
            std::memcpy(ptr, &it->second.original_inst, sizeof(u32));
            sys_icache_invalidate(ptr, sizeof(u32));
        }
        MakePageExecutable(host_addr);
    }
    breakpoints_.erase(it);
    LOG_DEBUG("Debugger: breakpoint removed at 0x%llx", host_addr);
}

void EmuDebugger::ClearAllBreakpoints() {
    std::lock_guard<std::mutex> lock(breakpoint_mutex_);
    breakpoints_.clear();
    LOG_DEBUG("Debugger: all breakpoints cleared");
}

bool EmuDebugger::HasBreakpoint(u64 guest_addr) const {
    std::lock_guard<std::mutex> lock(breakpoint_mutex_);
    auto it = breakpoints_.find(guest_addr);
    return it != breakpoints_.end() && it->second.enabled;
}

std::vector<BreakpointInfo> EmuDebugger::GetBreakpoints() const {
    std::lock_guard<std::mutex> lock(breakpoint_mutex_);
    std::vector<BreakpointInfo> result;
    for (auto& [addr, bp] : breakpoints_) {
        result.push_back({addr, bp.enabled, bp.hit_count, bp.temporary});
    }
    return result;
}

// ── 寄存器管理 ───────────────────────────────────────
void EmuDebugger::CaptureRegisters(const GuestThreadState& gs) {
    std::lock_guard<std::mutex> lock(reg_mutex_);
    for (int i = 0; i < 31; i++) {
        last_regs_.x[i] = gs.x[i];
    }
    last_regs_.sp = gs.sp;
    last_regs_.pc = gs.pc;
    last_regs_.pstate = 0;
    last_regs_.tpidrro_el0 = 0;
}

CpuRegisterSnapshot EmuDebugger::GetLastRegisters() const {
    std::lock_guard<std::mutex> lock(reg_mutex_);
    return last_regs_;
}

void EmuDebugger::SetRegister(int index, u64 value) {
    if (index < 0 || index >= 31) return;
    std::lock_guard<std::mutex> lock(reg_mutex_);
    last_regs_.x[index] = value;
}

// ── 执行控制 ─────────────────────────────────────────
void EmuDebugger::Pause() {
    paused_.store(true);
    LOG_DEBUG("Debugger: execution paused");
}

void EmuDebugger::Continue() {
    // 重新应用所有断点
    ApplyBreakpoints();
    paused_.store(false);
    stepping_.store(false);
    LOG_DEBUG("Debugger: execution continued");
}

void EmuDebugger::StepOver() {
    // 单步：恢复所有断点（当前断点已被临时移除），设目标地址后继续
    ApplyBreakpoints();
    paused_.store(false);
    stepping_.store(true);
    {
        std::lock_guard<std::mutex> lock(reg_mutex_);
        step_addr_.store(last_regs_.pc + 4);
    }
    LOG_DEBUG("Debugger: step over to 0x%llx", last_regs_.pc + 4);
}

// ── 核心集成 ─────────────────────────────────────────
bool EmuDebugger::OnBreakpoint(u64 host_addr, const GuestThreadState& gs) {
    CaptureRegisters(gs);

    bool is_bp = false;
    bool is_temporary = false;
    {
        std::lock_guard<std::mutex> lock(breakpoint_mutex_);
        auto it = breakpoints_.find(host_addr);
        if (it != breakpoints_.end() && it->second.enabled) {
            it->second.hit_count++;
            is_bp = true;
            is_temporary = it->second.temporary;

            // 恢复原始指令（临时移除断点，以便单步执行）
            // 页可能为 RX，需临时降级再恢复
            if (it->second.patched && memory_) {
                MakePageWritable(host_addr);
                auto* ptr = memory_->Pointer(ToGuestRel(memory_, host_addr));
                if (ptr && it->second.original_inst != 0) {
                    std::memcpy(ptr, &it->second.original_inst, sizeof(u32));
                    sys_icache_invalidate(ptr, sizeof(u32));
                    it->second.patched = false;
                }
                MakePageExecutable(host_addr);
                if (ptr) {
                    LOG_DEBUG("Debugger: restored original insn at 0x%llx", host_addr);
                }
            }

            if (is_temporary) {
                breakpoints_.erase(it);
            }
        }
    }

    if (is_bp) {
        u32 hit = [&]() -> u32 {
            std::lock_guard<std::mutex> lock(breakpoint_mutex_);
            auto it = breakpoints_.find(host_addr);
            return it != breakpoints_.end() ? it->second.hit_count : 0;
        }();
        LOG_INFO("Debugger: breakpoint hit at 0x%llx (hit #%u)", host_addr, hit);
        paused_.store(true);
        if (bp_callback_) {
            bp_callback_(host_addr, last_regs_);
        }
    }

    // 单步模式：在目标地址暂停
    if (stepping_.load() && host_addr == step_addr_.load()) {
        paused_.store(true);
        stepping_.store(false);
        LOG_DEBUG("Debugger: step complete at 0x%llx", host_addr);
        is_bp = true;
    }

    return is_bp;
}

// ── 内存查看 ─────────────────────────────────────────
std::vector<u8> EmuDebugger::ReadMemory(u64 host_addr, size_t size) const {
    if (!memory_) return {};
    u64 guest_rel = ToGuestRel(memory_, host_addr);
    std::vector<u8> buf(size);
    for (size_t i = 0; i < size; i++) {
        u8 val = 0;
        if (memory_->Read(guest_rel + i, &val) == Result::Success) {
            buf[i] = val;
        } else {
            buf[i] = 0;
        }
    }
    return buf;
}

bool EmuDebugger::WriteMemory(u64 host_addr, const std::vector<u8>& data) {
    if (!memory_) return false;
    u64 guest_rel = ToGuestRel(memory_, host_addr);
    for (size_t i = 0; i < data.size(); i++) {
        if (memory_->Write(guest_rel + i, data[i]) != Result::Success) {
            return false;
        }
    }
    return true;
}

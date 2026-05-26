#pragma once

#include "common/Types.h"
#include "cpu/ExceptionHandler.h"
#include "memory/Memory.h"

#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>

// ── 断点类型 ────────────────────────────────────────────
enum class BreakpointType : u8 {
    Code = 0,       // 执行断点
};

// ── 断点信息 ────────────────────────────────────────────
struct BreakpointInfo {
    u64 guest_address = 0;          // 访客地址
    bool enabled = true;
    u32 hit_count = 0;              // 命中次数
    bool temporary = false;         // 命中后自动移除
};

// ── CPU 寄存器快照 ──────────────────────────────────────
struct CpuRegisterSnapshot {
    u64 x[31];          // x0 - x30
    u64 sp;
    u64 pc;
    u64 pstate;
    u64 tpidrro_el0;    // TLS 基址寄存器
};

// ── 调试器 ──────────────────────────────────────────────
// 管理断点、提供寄存器访问、支持暂停/继续/单步
class EmuDebugger {
public:
    EmuDebugger();

    // ── 断点管理 ─────────────────────────────────────
    void SetBreakpoint(u64 guest_addr);
    void RemoveBreakpoint(u64 guest_addr);
    void ClearAllBreakpoints();
    bool HasBreakpoint(u64 guest_addr) const;
    std::vector<BreakpointInfo> GetBreakpoints() const;
    size_t GetBreakpointCount() const { return breakpoints_.size(); }

    // ── 寄存器管理 ───────────────────────────────────
    void CaptureRegisters(const GuestThreadState& gs);
    CpuRegisterSnapshot GetLastRegisters() const;
    void SetRegister(int index, u64 value);

    // ── 执行控制 ─────────────────────────────────────
    void Pause();
    void Continue();
    void StepOver();
    bool IsPaused() const { return paused_.load(); }
    bool IsStepping() const { return stepping_.load(); }

    // ── 事件回调 ─────────────────────────────────────
    using BreakpointHitCallback = std::function<void(u64 addr, const CpuRegisterSnapshot&)>;
    void SetBreakpointCallback(BreakpointHitCallback cb) { bp_callback_ = std::move(cb); }

    // ── 核心集成 ─────────────────────────────────────
    // 由 ExceptionHandler 在 SIGTRAP 时调用
    // 返回 true 表示断点已处理（应跳过正常 SVC 分发）
    bool OnBreakpoint(u64 guest_addr, const GuestThreadState& gs);

    // 应用/恢复断点补丁（写 BRK 指令到访存）
    Result ApplyBreakpoints();
    Result RestoreBreakpoints();

    // ── 内存查看 ─────────────────────────────────────
    std::vector<u8> ReadMemory(u64 addr, size_t size) const;
    bool WriteMemory(u64 addr, const std::vector<u8>& data);
    void SetMemory(Memory* mem) { memory_ = mem; }

private:
    // 在访存中写入 BRK 指令
    static u32 MakeBrkDebug(u64 addr);
    Result PatchBreakpoint(u64 addr, bool set);

    struct Breakpoint {
        bool enabled = true;
        u32 hit_count = 0;
        bool temporary = false;
        u32 original_inst = 0;  // 保存的原始指令
        bool patched = false;   // 当前是否已打补丁
    };

    std::unordered_map<u64, Breakpoint> breakpoints_;
    mutable std::mutex breakpoint_mutex_;

    CpuRegisterSnapshot last_regs_{};
    mutable std::mutex reg_mutex_;

    std::atomic<bool> paused_{false};
    std::atomic<bool> stepping_{false};
    std::atomic<u64> step_addr_{0};

    BreakpointHitCallback bp_callback_;
    Memory* memory_ = nullptr;
};

// ── 全局单例 ────────────────────────────────────────────
EmuDebugger& GlobalDebugger();

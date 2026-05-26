#pragma once

#include "common/Types.h"
#include "cpu/Debugger.h"
#include <string>
#include <vector>
#include <fstream>

// ── 访客状态快照 ──────────────────────────────────────────────
// 捕获完整的 CPU 寄存器、关键 GPU 状态和最近的 SVC 历史。
// 用于调试时回溯和自动化测试的黄金快照比较。

struct GuestSnapshot {
    // CPU 状态
    CpuRegisterSnapshot regs{};
    u64 tls_base = 0;
    u32 thread_id = 0;

    // GPU 状态关键词段
    struct GpuState {
        u64 shader_hashes[6] = {};   // 当前绑定的 shader hash
        u32 rt_count = 0;
        u32 vp_width = 0;
        u32 vp_height = 0;
        u64 draw_count = 0;
    } gpu;

    // 最近的 SVC 调用序列 (最多 128 条)
    std::vector<u32> recent_svcs;

    // 时间戳
    u64 timestamp = 0;

    // 打印友好的摘要
    std::string Summary() const;
};

// ── 快照管理器 ────────────────────────────────────────────────
class SnapshotManager {
public:
    static SnapshotManager& Instance();

    // ── 快照操作 ──────────────────────────────────────
    // 捕获当前完整状态
    GuestSnapshot Capture();

    // 保存到文件 (JSON 格式)
    bool SaveToFile(const std::string& path);
    // 从文件加载
    GuestSnapshot LoadFromFile(const std::string& path);

    // ── 自动快照 ──────────────────────────────────────
    // 每隔 N 次 SVC 调用自动保存一次快照
    void SetAutoSnapshotInterval(u32 svc_count);  // 0 = 禁用
    u32 GetAutoSnapshotInterval() const { return auto_interval_; }

    // 每 N 帧自动保存一次
    void SetAutoSnapshotPerFrame(u32 frame_count);  // 0 = 禁用
    u32 GetAutoSnapshotPerFrame() const { return auto_frame_interval_; }

    // 由 SVC trace 调用，检查是否需要自动快照
    void OnSvcCall(u32 svc_num);

    // 由渲染循环调用，检查是否需要自动帧快照
    void OnFrameComplete();

    // 获取最近一次自动快照
    GuestSnapshot GetLastAutoSnapshot() const;

    // ── 历史快照 ──────────────────────────────────────
    // 保存所有自动快照 (最多 max_history 条)
    void SetMaxHistory(size_t max_history);
    const std::vector<GuestSnapshot>& GetHistory() const { return history_; }

    // ── 快照目录 ──────────────────────────────────────
    void SetSnapshotDir(const std::string& dir);
    const std::string& GetSnapshotDir() const { return snapshot_dir_; }

private:
    SnapshotManager();
    ~SnapshotManager() = default;

    std::string snapshot_dir_ = "/tmp/m1switch_snaps";
    u32 auto_interval_ = 0;        // SVC 间隔
    u32 auto_frame_interval_ = 0;  // 帧间隔
    u32 svc_counter_ = 0;
    u32 frame_counter_ = 0;
    size_t max_history_ = 64;
    std::vector<GuestSnapshot> history_;
    GuestSnapshot last_auto_snapshot_;
};
#pragma once

#include "common/Types.h"
#include "common/Log.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <sys/mman.h>

// ── 全链路追踪引擎 ────────────────────────────────────────────
// 零分配设计：预分配 mmap 环形缓冲，记录事件无需 malloc。
// 通道隔离：每个通道独立开关，调试时只开需要的通道。
// 延迟落盘：内存缓冲满或手动 Flush() 时才写文件。

enum class TraceChannel : u8 {
    SVC      = 0,   // 系统调用
    IPC      = 1,   // 服务间通信
    GPU_CMD  = 2,   // GPU 命令
    MEM      = 3,   // 内存访问
    CPU_EXEC = 4,   // CPU 执行流
    THREAD   = 5,   // 线程生命周期
    COUNT    = 6,
};

constexpr const char* TraceChannelNames[] = {
    "SVC", "IPC", "GPU_CMD", "MEM", "CPU_EXEC", "THREAD"
};

// ── 追踪事件 ──────────────────────────────────────────────────
// 紧凑布局 64 字节，适合高速写入环形缓冲
struct TraceEvent {
    u64 timestamp;       // 8  mach_absolute_time
    u64 guest_pc;        // 8  触发时的访客 PC (0 = 主机侧事件)
    u64 args[2];         // 16 前2个关键参数
    u64 result;           // 8  返回值
    u32 event_id;        // 4  SVC号 / IPC cmd / GPU method
    u32 thread_id;       // 4  访客线程 ID
    TraceChannel channel; // 1  通道
    u8  extra_size;      // 1  额外数据大小 (0-255)
    u16 _reserved;       // 2  保留

    void Clear() {
        std::memset(this, 0, sizeof(*this));
    }

    std::string Summary() const;
};

static_assert(sizeof(TraceEvent) == 56, "TraceEvent 应为 56 字节");

// ── 追踪过滤器 ────────────────────────────────────────────────
using TraceFilterFn = std::function<bool(const TraceEvent&)>;

// ── 追踪引擎 ──────────────────────────────────────────────────
class TraceEngine {
public:
    static TraceEngine& Instance();

    // ── 通道控制 ─────────────────────────────────────
    void EnableChannel(TraceChannel ch, bool enable = true);
    void DisableChannel(TraceChannel ch);
    void EnableAll();
    void DisableAll();
    bool IsChannelEnabled(TraceChannel ch) const;

    // ── 过滤器 ────────────────────────────────────────
    void SetFilter(TraceChannel ch, TraceFilterFn filter);
    void ClearFilter(TraceChannel ch);

    // ── 事件记录 ──────────────────────────────────────
    // 固定参数版本 (最快，无额外数据)
    void Record(TraceChannel ch, u32 event_id,
                u64 arg0 = 0, u64 arg1 = 0,
                u64 result = 0);

    // 带额外数据的版本 (用于 IPC 消息体等)
    void RecordWithExtra(TraceChannel ch, u32 event_id,
                         const u8* extra_data, u8 extra_size,
                         u64 arg0 = 0, u64 arg1 = 0,
                         u64 result = 0);

    // ── 设置当前线程信息 (由 SVC 调度器设置) ────────
    void SetCurrentGuestPc(u64 pc);
    void SetCurrentThreadId(u32 tid);
    u64 GetCurrentGuestPc() const;
    u32 GetCurrentThreadId() const;

    // ── 落盘 ──────────────────────────────────────────
    void Flush();
    void SetOutputFile(const std::string& path);
    void EnableFileOutput(bool enable = true);

    // ── 查询 ──────────────────────────────────────────
    size_t GetEventCount(TraceChannel ch) const;
    size_t GetTotalEventCount() const;
    std::vector<TraceEvent> Query(TraceChannel ch,
                                   u64 from_ts = 0,
                                   u64 to_ts = UINT64_MAX,
                                   u32 max_count = 1000) const;

    // ── 统计 ──────────────────────────────────────────
    struct Stats {
        std::array<u64, (size_t)TraceChannel::COUNT> events_per_channel{};
        u64 total_events = 0;
        u64 dropped_events = 0;
        u64 ring_buffer_capacity = 0;
    };
    Stats GetStats() const;

    // ── 全局开关 ──────────────────────────────────────
    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    // ── 清空 ───────────────────────────────────────────
    void Clear();

private:
    TraceEngine();
    ~TraceEngine();
    TraceEngine(const TraceEngine&) = delete;
    TraceEngine& operator=(const TraceEngine&) = delete;

    // 环形缓冲区实现
    static constexpr u32 RING_BUFFER_SIZE = 1 << 20;  // 1M events = 64MB
    TraceEvent* ring_buffer_ = nullptr;   // mmap 分配
    std::atomic<u64> write_pos_{0};       // 写入位置 (单调递增)
    std::atomic<u64> read_pos_{0};        // 读取位置

    // 通道控制
    std::array<std::atomic<bool>, (size_t)TraceChannel::COUNT> channel_enabled_{};
    std::array<TraceFilterFn, (size_t)TraceChannel::COUNT> filters_{};
    std::array<std::atomic<u64>, (size_t)TraceChannel::COUNT> channel_event_count_{};

    // 线程本地信息
    static thread_local u64 tl_guest_pc;
    static thread_local u32 tl_thread_id;

    // 全局开关
    std::atomic<bool> enabled_{true};

    // 文件输出
    std::string output_path_;
    std::ofstream output_file_;
    std::atomic<bool> file_output_enabled_{false};
    mutable std::mutex file_mutex_;

    // 统计
    std::atomic<u64> total_events_{0};
    std::atomic<u64> dropped_events_{0};

    // 内部方法
    u64 AllocateSlot(u8 extra_size);
    void WriteToFile(const TraceEvent& evt, const u8* extra);
};

// ── 便捷宏 ─────────────────────────────────────────────────────
// 在性能关键路径使用，通道禁用时零开销

#define TRACE_SVC(svc_num, a0, a1, result) \
    do { \
        if (TraceEngine::Instance().IsEnabled() && \
            TraceEngine::Instance().IsChannelEnabled(TraceChannel::SVC)) { \
            TraceEngine::Instance().Record(TraceChannel::SVC, svc_num, \
                                           a0, a1, result); \
        } \
    } while(0)

#define TRACE_SVC_EXTRA(svc_num, data, size, a0, a1, result) \
    do { \
        if (TraceEngine::Instance().IsEnabled() && \
            TraceEngine::Instance().IsChannelEnabled(TraceChannel::SVC)) { \
            TraceEngine::Instance().RecordWithExtra(TraceChannel::SVC, svc_num, \
                                                     data, (u8)(size), \
                                                     a0, a1, result); \
        } \
    } while(0)

#define TRACE_IPC(cmd_id, a0, a1, result) \
    do { \
        if (TraceEngine::Instance().IsEnabled() && \
            TraceEngine::Instance().IsChannelEnabled(TraceChannel::IPC)) { \
            TraceEngine::Instance().Record(TraceChannel::IPC, cmd_id, \
                                           a0, a1, result); \
        } \
    } while(0)

#define TRACE_IPC_EXTRA(cmd_id, data, size, a0, a1, result) \
    do { \
        if (TraceEngine::Instance().IsEnabled() && \
            TraceEngine::Instance().IsChannelEnabled(TraceChannel::IPC)) { \
            TraceEngine::Instance().RecordWithExtra(TraceChannel::IPC, cmd_id, \
                                                     data, (u8)(size), \
                                                     a0, a1, result); \
        } \
    } while(0)

#define TRACE_GPU(method, a0, a1, result) \
    do { \
        if (TraceEngine::Instance().IsEnabled() && \
            TraceEngine::Instance().IsChannelEnabled(TraceChannel::GPU_CMD)) { \
            TraceEngine::Instance().Record(TraceChannel::GPU_CMD, method, \
                                           a0, a1, result); \
        } \
    } while(0)

#define TRACE_THREAD(event_id, a0, a1) \
    do { \
        if (TraceEngine::Instance().IsEnabled() && \
            TraceEngine::Instance().IsChannelEnabled(TraceChannel::THREAD)) { \
            TraceEngine::Instance().Record(TraceChannel::THREAD, event_id, \
                                           a0, a1, 0); \
        } \
    } while(0)

// 线程事件 ID
enum ThreadEventId : u32 {
    THREAD_CREATE   = 1,
    THREAD_START    = 2,
    THREAD_EXIT     = 3,
    THREAD_WAIT     = 4,
    THREAD_WAKE     = 5,
};
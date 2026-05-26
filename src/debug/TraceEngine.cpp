#include "debug/TraceEngine.h"
#include <mach/mach_time.h>
#include <sstream>

// ── 线程局部存储 ──────────────────────────────────────────────
thread_local u64 TraceEngine::tl_guest_pc = 0;
thread_local u32 TraceEngine::tl_thread_id = 0;

// ── 辅助：获取高精度时间戳 ────────────────────────────────────
static u64 FastTimestamp() {
    return mach_absolute_time();
}

// ── TraceEvent 格式化 ──────────────────────────────────────────
std::string TraceEvent::Summary() const {
    std::ostringstream oss;
    const char* ch_name = (u32)channel < (u32)TraceChannel::COUNT
                              ? TraceChannelNames[(u32)channel]
                              : "UNKNOWN";
    oss << "[" << ch_name << "] ";
    oss << "event=" << event_id << " ";
    oss << "tid=" << thread_id << " ";
    if (guest_pc) oss << "pc=0x" << std::hex << guest_pc << " ";
    oss << "args=(";
    oss << std::hex;
    for (int i = 0; i < 4; i++) {
        if (i) oss << ", ";
        oss << "0x" << args[i];
    }
    oss << std::dec << ") ";
    oss << "result=0x" << std::hex << result;
    if (extra_size) oss << " extra=" << extra_size << "B";
    return oss.str();
}

// ── 单例 ───────────────────────────────────────────────────────
TraceEngine& TraceEngine::Instance() {
    static TraceEngine instance;
    return instance;
}

TraceEngine::TraceEngine() {
    // 分配环形缓冲区 (mmap 避免物理内存占用)
    ring_buffer_ = (TraceEvent*)mmap(
        nullptr,
        RING_BUFFER_SIZE * sizeof(TraceEvent),
        PROT_READ | PROT_WRITE,
        MAP_ANONYMOUS | MAP_PRIVATE,
        -1, 0);
    if (ring_buffer_ == MAP_FAILED) {
        ring_buffer_ = nullptr;
        LOG_ERROR("TraceEngine: mmap 失败，追踪功能不可用");
    }

    // 默认只开启 SVC 和 IPC 通道
    for (size_t i = 0; i < (size_t)TraceChannel::COUNT; i++) {
        channel_enabled_[i].store(false, std::memory_order_relaxed);
        channel_event_count_[i].store(0, std::memory_order_relaxed);
    }
    channel_enabled_[(size_t)TraceChannel::SVC].store(true);
    channel_enabled_[(size_t)TraceChannel::IPC].store(true);
}

TraceEngine::~TraceEngine() {
    Flush();
    if (ring_buffer_ && ring_buffer_ != MAP_FAILED) {
        munmap(ring_buffer_, RING_BUFFER_SIZE * sizeof(TraceEvent));
    }
    if (output_file_.is_open()) {
        output_file_.close();
    }
}

// ── 通道控制 ───────────────────────────────────────────────────
void TraceEngine::EnableChannel(TraceChannel ch, bool enable) {
    if ((u32)ch < (u32)TraceChannel::COUNT)
        channel_enabled_[(u32)ch].store(enable, std::memory_order_relaxed);
}

void TraceEngine::DisableChannel(TraceChannel ch) {
    EnableChannel(ch, false);
}

void TraceEngine::EnableAll() {
    for (size_t i = 0; i < (size_t)TraceChannel::COUNT; i++)
        channel_enabled_[i].store(true, std::memory_order_relaxed);
}

void TraceEngine::DisableAll() {
    for (size_t i = 0; i < (size_t)TraceChannel::COUNT; i++)
        channel_enabled_[i].store(false, std::memory_order_relaxed);
}

bool TraceEngine::IsChannelEnabled(TraceChannel ch) const {
    if ((u32)ch >= (u32)TraceChannel::COUNT) return false;
    return channel_enabled_[(u32)ch].load(std::memory_order_relaxed);
}

// ── 过滤器 ──────────────────────────────────────────────────────
void TraceEngine::SetFilter(TraceChannel ch, TraceFilterFn filter) {
    if ((u32)ch < (u32)TraceChannel::COUNT)
        filters_[(u32)ch] = std::move(filter);
}

void TraceEngine::ClearFilter(TraceChannel ch) {
    if ((u32)ch < (u32)TraceChannel::COUNT)
        filters_[(u32)ch] = nullptr;
}

// ── 环形缓冲槽位分配 ──────────────────────────────────────────
u64 TraceEngine::AllocateSlot(u8 extra_size) {
    // 每个事件额外数据可能跨越多个 TraceEvent 槽位
    u32 slots_needed = 1;
    if (extra_size > 0) {
        slots_needed += (extra_size + sizeof(TraceEvent) - 1) / sizeof(TraceEvent);
    }

    u64 pos = write_pos_.fetch_add(slots_needed, std::memory_order_relaxed);

    if (!ring_buffer_) {
        dropped_events_.fetch_add(1, std::memory_order_relaxed);
        return UINT64_MAX;
    }

    // 环形缓冲溢出检查
    u64 ring_idx = pos % RING_BUFFER_SIZE;
    if (pos - read_pos_.load(std::memory_order_relaxed) > RING_BUFFER_SIZE - slots_needed) {
        dropped_events_.fetch_add(1, std::memory_order_relaxed);
        // 强制推进读指针
        read_pos_.store(pos - RING_BUFFER_SIZE + slots_needed, std::memory_order_relaxed);
    }

    return pos;
}

// ── 事件记录 (固定参数) ────────────────────────────────────────
void TraceEngine::Record(TraceChannel ch, u32 event_id,
                          u64 arg0, u64 arg1,
                          u64 result) {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    if (!IsChannelEnabled(ch)) return;

    // 过滤器检查
    auto idx = (u32)ch;
    if (idx < (u32)TraceChannel::COUNT && filters_[idx]) {
        TraceEvent temp;
        temp.Clear();
        temp.channel = ch;
        temp.event_id = event_id;
        temp.args[0] = arg0;
        temp.args[1] = arg1;
        temp.result = result;
        if (!filters_[idx](temp)) return;
    }

    u64 pos = AllocateSlot(0);
    if (pos == UINT64_MAX) return;

    u64 ring_idx = pos % RING_BUFFER_SIZE;
    TraceEvent& evt = ring_buffer_[ring_idx];
    evt.Clear();
    evt.timestamp = FastTimestamp();
    evt.guest_pc = tl_guest_pc;
    evt.thread_id = tl_thread_id;
    evt.channel = ch;
    evt.event_id = event_id;
    evt.args[0] = arg0;
    evt.args[1] = arg1;
    evt.result = result;
    evt.extra_size = 0;

    channel_event_count_[idx].fetch_add(1, std::memory_order_relaxed);
    total_events_.fetch_add(1, std::memory_order_relaxed);

    // 如果启用了文件输出，同步写入
    if (file_output_enabled_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (output_file_.is_open()) {
            output_file_.write(reinterpret_cast<const char*>(&evt), sizeof(TraceEvent));
        }
    }
}

// ── 事件记录 (带额外数据) ──────────────────────────────────────
void TraceEngine::RecordWithExtra(TraceChannel ch, u32 event_id,
                                   const u8* extra_data, u8 extra_size,
                                   u64 arg0, u64 arg1,
                                   u64 result) {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    if (!IsChannelEnabled(ch)) return;

    u64 pos = AllocateSlot(extra_size);
    if (pos == UINT64_MAX) return;

    u64 ring_idx = pos % RING_BUFFER_SIZE;
    TraceEvent& evt = ring_buffer_[ring_idx];
    evt.Clear();
    evt.timestamp = FastTimestamp();
    evt.guest_pc = tl_guest_pc;
    evt.thread_id = tl_thread_id;
    evt.channel = ch;
    evt.event_id = event_id;
    evt.args[0] = arg0;
    evt.args[1] = arg1;
    evt.result = result;
    evt.extra_size = extra_size;

    // 写入额外数据到后续槽位
    if (extra_size > 0 && extra_data) {
        u32 slots_for_extra = (extra_size + sizeof(TraceEvent) - 1) / sizeof(TraceEvent);
        for (u32 i = 0; i < slots_for_extra; i++) {
            u64 data_idx = (ring_idx + 1 + i) % RING_BUFFER_SIZE;
            u32 offset = i * sizeof(TraceEvent);
            u32 remaining = (extra_size > offset) ? extra_size - offset : 0;
            u32 copy_size = (remaining < sizeof(TraceEvent)) ? remaining : sizeof(TraceEvent);
            if (copy_size > 0) {
                std::memcpy(&ring_buffer_[data_idx], extra_data + offset, copy_size);
            }
        }
    }

    auto idx = (u32)ch;
    channel_event_count_[idx].fetch_add(1, std::memory_order_relaxed);
    total_events_.fetch_add(1, std::memory_order_relaxed);

    // 文件输出
    if (file_output_enabled_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (output_file_.is_open()) {
            output_file_.write(reinterpret_cast<const char*>(&evt), sizeof(TraceEvent));
            if (extra_size > 0 && extra_data) {
                output_file_.write(reinterpret_cast<const char*>(extra_data), extra_size);
            }
        }
    }
}

// ── 线程信息 ───────────────────────────────────────────────────
void TraceEngine::SetCurrentGuestPc(u64 pc) { tl_guest_pc = pc; }
void TraceEngine::SetCurrentThreadId(u32 tid) { tl_thread_id = tid; }
u64 TraceEngine::GetCurrentGuestPc() const { return tl_guest_pc; }
u32 TraceEngine::GetCurrentThreadId() const { return tl_thread_id; }

// ── 落盘 ───────────────────────────────────────────────────────
void TraceEngine::Flush() {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (output_file_.is_open()) {
        output_file_.flush();
    }
}

void TraceEngine::SetOutputFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(file_mutex_);
    if (output_file_.is_open()) {
        output_file_.flush();
        output_file_.close();
    }
    output_path_ = path;
    output_file_.open(path, std::ios::binary | std::ios::app);
    if (!output_file_.is_open()) {
        LOG_ERROR("TraceEngine: 无法打开输出文件 %s", path.c_str());
    }
}

void TraceEngine::EnableFileOutput(bool enable) {
    file_output_enabled_.store(enable, std::memory_order_relaxed);
}

// ── 查询 ───────────────────────────────────────────────────────
size_t TraceEngine::GetEventCount(TraceChannel ch) const {
    if ((u32)ch >= (u32)TraceChannel::COUNT) return 0;
    return channel_event_count_[(u32)ch].load(std::memory_order_relaxed);
}

size_t TraceEngine::GetTotalEventCount() const {
    return total_events_.load(std::memory_order_relaxed);
}

std::vector<TraceEvent> TraceEngine::Query(TraceChannel ch,
                                             u64 from_ts, u64 to_ts,
                                             u32 max_count) const {
    std::vector<TraceEvent> result;
    if (!ring_buffer_) return result;

    u64 current_read = read_pos_.load(std::memory_order_relaxed);
    u64 current_write = write_pos_.load(std::memory_order_relaxed);

    // 从最新的开始往前找
    for (u64 i = current_write; i > current_read && result.size() < max_count; i--) {
        const TraceEvent& evt = ring_buffer_[(i - 1) % RING_BUFFER_SIZE];
        if (evt.channel != ch) continue;
        if (evt.timestamp < from_ts || evt.timestamp > to_ts) continue;
        result.push_back(evt);
    }

    // 按时间排序
    std::sort(result.begin(), result.end(),
              [](const TraceEvent& a, const TraceEvent& b) {
                  return a.timestamp < b.timestamp;
              });

    return result;
}

// ── 统计 ───────────────────────────────────────────────────────
TraceEngine::Stats TraceEngine::GetStats() const {
    Stats stats;
    stats.total_events = total_events_.load(std::memory_order_relaxed);
    stats.dropped_events = dropped_events_.load(std::memory_order_relaxed);
    stats.ring_buffer_capacity = RING_BUFFER_SIZE;
    for (size_t i = 0; i < (size_t)TraceChannel::COUNT; i++) {
        stats.events_per_channel[i] = channel_event_count_[i].load(std::memory_order_relaxed);
    }
    return stats;
}

// ── 全局开关 ────────────────────────────────────────────────────
void TraceEngine::SetEnabled(bool enabled) {
    enabled_.store(enabled, std::memory_order_relaxed);
}

bool TraceEngine::IsEnabled() const {
    return enabled_.load(std::memory_order_relaxed);
}

// ── 清空 ────────────────────────────────────────────────────────
void TraceEngine::Clear() {
    write_pos_.store(0, std::memory_order_relaxed);
    read_pos_.store(0, std::memory_order_relaxed);
    total_events_.store(0, std::memory_order_relaxed);
    dropped_events_.store(0, std::memory_order_relaxed);
    for (size_t i = 0; i < (size_t)TraceChannel::COUNT; i++) {
        channel_event_count_[i].store(0, std::memory_order_relaxed);
    }
}
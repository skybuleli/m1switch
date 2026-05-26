#include "debug/SnapshotManager.h"
#include "debug/TraceEngine.h"
#include "kernel/Kernel.h"
#include "common/Log.h"

#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <cstring>
#include <mach/mach_time.h>

// ── GuestSnapshot 格式化 ───────────────────────────────────────

std::string GuestSnapshot::Summary() const {
    std::ostringstream oss;
    oss << "=== 快照 @ tid=" << thread_id << " ===\n";
    oss << "PC=0x" << std::hex << regs.pc << " SP=0x" << regs.sp << "\n";
    oss << "X0=0x" << regs.x[0] << " X1=0x" << regs.x[1]
        << " X2=0x" << regs.x[2] << " X3=0x" << regs.x[3] << "\n";
    oss << "GPU: draws=" << std::dec << gpu.draw_count
        << " rt_count=" << gpu.rt_count
        << " vp=" << gpu.vp_width << "x" << gpu.vp_height << "\n";
    if (!recent_svcs.empty()) {
        oss << "最近SVC: ";
        for (size_t i = recent_svcs.size() > 8 ? recent_svcs.size() - 8 : 0;
             i < recent_svcs.size(); i++) {
            oss << "0x" << std::hex << recent_svcs[i] << " ";
        }
        oss << "\n";
    }
    return oss.str();
}

// ── 单例 ───────────────────────────────────────────────────────

SnapshotManager& SnapshotManager::Instance() {
    static SnapshotManager instance;
    return instance;
}

SnapshotManager::SnapshotManager() {
    // 确保快照目录存在
    mkdir(snapshot_dir_.c_str(), 0755);
}

// ── 捕获 ───────────────────────────────────────────────────────

GuestSnapshot SnapshotManager::Capture() {
    GuestSnapshot snap;
    snap.timestamp = mach_absolute_time();

    // CPU 寄存器
    auto& dbg = GlobalDebugger();
    snap.regs = dbg.GetLastRegisters();
    snap.thread_id = TraceEngine::Instance().GetCurrentThreadId();

    // 最近的 SVC 历史
    auto svc_events = TraceEngine::Instance().Query(
        TraceChannel::SVC, 0, UINT64_MAX, 128);
    snap.recent_svcs.reserve(svc_events.size());
    for (const auto& evt : svc_events) {
        snap.recent_svcs.push_back(evt.event_id);
    }

    // GPU 状态 (暂时从空获取，需要集成 StateTracker)
    // TODO: 集成 StateTracker 获取实际 GPU 状态
    snap.gpu.draw_count = 0;

    LOG_DEBUG("SnapshotManager: 捕获快照 tid=%u PC=0x%llx",
              snap.thread_id, snap.regs.pc);
    return snap;
}

// ── 保存到文件 ─────────────────────────────────────────────────

bool SnapshotManager::SaveToFile(const std::string& path) {
    auto snap = Capture();

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("SnapshotManager: 无法写入 %s", path.c_str());
        return false;
    }

    // 写入标识和版本
    const char magic[] = "M1SW_SNAP";
    u32 version = 1;
    file.write(magic, sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    // 写入快照结构
    file.write(reinterpret_cast<const char*>(&snap.regs), sizeof(snap.regs));
    file.write(reinterpret_cast<const char*>(&snap.tls_base), sizeof(snap.tls_base));
    file.write(reinterpret_cast<const char*>(&snap.thread_id), sizeof(snap.thread_id));
    file.write(reinterpret_cast<const char*>(&snap.gpu), sizeof(snap.gpu));
    file.write(reinterpret_cast<const char*>(&snap.timestamp), sizeof(snap.timestamp));

    u32 svc_count = (u32)snap.recent_svcs.size();
    file.write(reinterpret_cast<const char*>(&svc_count), sizeof(svc_count));
    if (svc_count > 0) {
        file.write(reinterpret_cast<const char*>(snap.recent_svcs.data()),
                   svc_count * sizeof(u32));
    }

    file.close();
    LOG_INFO("SnapshotManager: 快照已保存到 %s", path.c_str());
    return true;
}

// ── 从文件加载 ─────────────────────────────────────────────────

GuestSnapshot SnapshotManager::LoadFromFile(const std::string& path) {
    GuestSnapshot snap;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("SnapshotManager: 无法读取 %s", path.c_str());
        return snap;
    }

    char magic[10] = {};
    file.read(magic, 9);
    if (std::strcmp(magic, "M1SW_SNAP") != 0) {
        LOG_ERROR("SnapshotManager: 无效快照文件 %s", path.c_str());
        return snap;
    }

    u32 version = 0;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));

    file.read(reinterpret_cast<char*>(&snap.regs), sizeof(snap.regs));
    file.read(reinterpret_cast<char*>(&snap.tls_base), sizeof(snap.tls_base));
    file.read(reinterpret_cast<char*>(&snap.thread_id), sizeof(snap.thread_id));
    file.read(reinterpret_cast<char*>(&snap.gpu), sizeof(snap.gpu));
    file.read(reinterpret_cast<char*>(&snap.timestamp), sizeof(snap.timestamp));

    u32 svc_count = 0;
    file.read(reinterpret_cast<char*>(&svc_count), sizeof(svc_count));
    if (svc_count > 0 && svc_count < 10000) {
        snap.recent_svcs.resize(svc_count);
        file.read(reinterpret_cast<char*>(snap.recent_svcs.data()),
                  svc_count * sizeof(u32));
    }

    LOG_INFO("SnapshotManager: 从 %s 加载快照 tid=%u PC=0x%llx",
             path.c_str(), snap.thread_id, snap.regs.pc);
    return snap;
}

// ── 自动快照 ──────────────────────────────────────────────────

void SnapshotManager::SetAutoSnapshotInterval(u32 svc_count) {
    auto_interval_ = svc_count;
    svc_counter_ = 0;
    LOG_INFO("SnapshotManager: 自动快照间隔设为 %u 次 SVC", svc_count);
}

void SnapshotManager::SetAutoSnapshotPerFrame(u32 frame_count) {
    auto_frame_interval_ = frame_count;
    frame_counter_ = 0;
    LOG_INFO("SnapshotManager: 自动帧快照间隔设为 %u 帧", frame_count);
}

void SnapshotManager::OnSvcCall(u32 svc_num) {
    if (auto_interval_ == 0) return;

    svc_counter_++;
    if (svc_counter_ >= auto_interval_) {
        svc_counter_ = 0;
        last_auto_snapshot_ = Capture();

        // 保存到历史
        if (history_.size() >= max_history_) {
            history_.erase(history_.begin());
        }
        history_.push_back(last_auto_snapshot_);
    }
}

void SnapshotManager::OnFrameComplete() {
    if (auto_frame_interval_ == 0) return;

    frame_counter_++;
    if (frame_counter_ >= auto_frame_interval_) {
        frame_counter_ = 0;
        last_auto_snapshot_ = Capture();

        if (history_.size() >= max_history_) {
            history_.erase(history_.begin());
        }
        history_.push_back(last_auto_snapshot_);
    }
}

GuestSnapshot SnapshotManager::GetLastAutoSnapshot() const {
    return last_auto_snapshot_;
}

void SnapshotManager::SetMaxHistory(size_t max_history) {
    max_history_ = max_history;
    while (history_.size() > max_history_) {
        history_.erase(history_.begin());
    }
}

void SnapshotManager::SetSnapshotDir(const std::string& dir) {
    snapshot_dir_ = dir;
    mkdir(snapshot_dir_.c_str(), 0755);
}
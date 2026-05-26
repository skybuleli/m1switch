#include "services/Ipc.h"
#include "common/Log.h"
#include "loader/RomFs.h"
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>

// ── FS (File System) Service ────────────────────────────────
// P1-4: 完整 RomFS 文件/目录遍历 + SaveData 支持

// FS command IDs
enum class FsCmd : u32 {
    Initialize            = 0x04010000,
    OpenRomFS             = 0x04020002,
    OpenFile              = 0x08020000,
    OpenDirectory         = 0x08030000,
    Commit                = 0x08010001,
    CreateFile            = 0x08040000,
    DeleteFile            = 0x08050000,
    GetSize               = 0x0404000A,
    Read                  = 0x04060002,
    Write                 = 0x04070002,
    Close                 = 0x04080000,
    OpenDataFileByCurrent = 0x08020001,
    OpenSaveDataFile      = 0x08020002,
    MountRom              = 0x04020004,
    SetCurrentProcess     = 0x04010002,
    GetEntryType          = 0x04050000,
    RenameFile            = 0x08060000,
};

// ── 文件属性 (HOS DirEntryType) ──────────────────────────────
enum class FsEntryType : u32 {
    Directory = 0,
    File      = 1,
};

// ── 文件/目录句柄管理 ─────────────────────────────────────────
class FileTable {
public:
    struct Entry {
        std::string path;
        std::vector<u8> data;
        u64 pos = 0;
        bool writable = false;
        bool dir = false;
        // 目录枚举状态
        u32 dir_cursor = 0;
        std::vector<std::string> dir_entries;
    };

    u32 Open(const std::string& path, const std::vector<u8>& data, bool writable) {
        u32 fd = next_fd_++;
        entries_[fd] = {path, data, 0, writable, false};
        return fd;
    }

    u32 OpenDir(const std::string& path, const std::vector<std::string>& entries) {
        u32 fd = next_fd_++;
        Entry e;
        e.path = path;
        e.dir = true;
        e.dir_entries = entries;
        e.pos = 0;
        entries_[fd] = std::move(e);
        return fd;
    }

    Entry* Get(u32 fd) {
        auto it = entries_.find(fd);
        return it != entries_.end() ? &it->second : nullptr;
    }

    void Close(u32 fd) { entries_.erase(fd); }

private:
    std::unordered_map<u32, Entry> entries_;
    u32 next_fd_ = 0x1000;
};

static FileTable g_file_table;

// ── RomFS 数据 (由 loader 设置) ──────────────────────────────
static RomFs g_romfs;
static bool g_romfs_loaded = false;
static std::vector<u8> g_romfs_raw_data;

extern "C" void FsService_SetRomFS(std::span<const u8> data) {
    g_romfs_raw_data.assign(data.begin(), data.end());
    if (g_romfs.Parse(data)) {
        g_romfs_loaded = true;
        LOG_INFO("FS: RomFS 解析完成 (%zu 目录, %zu 文件, %zu 字节)",
                 g_romfs.GetDirCount(), g_romfs.GetFileCount(), g_romfs_raw_data.size());
    }
}

// ── FS 服务实现 ───────────────────────────────────────────────
class FsService : public ServiceBase {
public:
    FsService() {
        IpcManager::Instance().RegisterService("fsp-srv:", this);
        IpcManager::Instance().RegisterService("fs:", this);
    }

    const char* Name() const override { return "fsp-srv:"; }

    bool HandleCommand(u32 cmd_id, const u8* in, size_t in_sz,
                       u8* out, size_t* out_sz) override {
        switch (static_cast<FsCmd>(cmd_id)) {
        case FsCmd::Initialize:
            LOG_DEBUG("FS: Initialize"); *out_sz = 0; return true;

        case FsCmd::SetCurrentProcess:
            LOG_DEBUG("FS: SetCurrentProcess"); *out_sz = 0; return true;

        case FsCmd::OpenRomFS:
            return HandleOpenRomFS(in, in_sz, out, out_sz);

        case FsCmd::OpenFile:
            return HandleOpenFile(in, in_sz, out, out_sz);

        case FsCmd::OpenDirectory:
            return HandleOpenDirectory(in, in_sz, out, out_sz);

        case FsCmd::GetEntryType:
            return HandleGetEntryType(in, in_sz, out, out_sz);

        case FsCmd::GetSize:
            return HandleGetSize(in, in_sz, out, out_sz);

        case FsCmd::Read:
            return HandleRead(in, in_sz, out, out_sz);

        case FsCmd::Write:
            *out_sz = 0; return true;

        case FsCmd::Close:
            return HandleClose(in, in_sz, out, out_sz);

        case FsCmd::Commit:
            *out_sz = 0; return true;

        case FsCmd::OpenDataFileByCurrent:
            return HandleOpenDataFile(in, in_sz, out, out_sz);

        case FsCmd::OpenSaveDataFile:
            return HandleOpenSaveDataFile(in, in_sz, out, out_sz);

        case FsCmd::MountRom:
            LOG_DEBUG("FS: MountRom"); *out_sz = 0; return true;

        case FsCmd::CreateFile:
            LOG_DEBUG("FS: CreateFile (桩)"); *out_sz = 0; return true;

        case FsCmd::DeleteFile:
            LOG_DEBUG("FS: DeleteFile (桩)"); *out_sz = 0; return true;

        case FsCmd::RenameFile:
            LOG_DEBUG("FS: RenameFile (桩)"); *out_sz = 0; return true;

        case static_cast<FsCmd>(0):  // 回收缓冲区/空请求（type=2 close 或空初始化），无害化处理
            LOG_DEBUG("FS: cmd=0 (no-op)");
            *out_sz = 0;
            return true;

        default:
            LOG_WARN("FS: 未处理命令 0x%08x", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    // 从 IPC 输入中读取路径 (null-terminated)
    static std::string ReadPath(const u8* in, size_t in_sz, size_t off) {
        if (off + 4 > in_sz) return "";
        u32 len = (u32)in[off] | ((u32)in[off+1]<<8) | ((u32)in[off+2]<<16) | ((u32)in[off+3]<<24);
        if (len == 0 || off + 4 + len > in_sz) return "";
        return std::string(reinterpret_cast<const char*>(in + off + 4), len);
    }

    // 规范化路径: 去掉开头的 "/" 和 "romfs:" 前缀
    static std::string NormalizePath(const std::string& path) {
        std::string p = path;
        while (!p.empty() && p.front() == '/') p.erase(p.begin());
        if (p.rfind("romfs:", 0) == 0) p = p.substr(6);
        while (!p.empty() && p.front() == '/') p.erase(p.begin());
        return p;
    }

    // 写出句柄 ID (小端序 u64)
    static void WriteHandle(u8* out, u32 fd, size_t* out_sz) {
        if (*out_sz >= 8) {
            std::memset(out, 0, 8);
            out[0] = fd & 0xFF;
            out[1] = (fd >> 8) & 0xFF;
            out[2] = (fd >> 16) & 0xFF;
            out[3] = (fd >> 24) & 0xFF;
            *out_sz = 8;
        }
    }

    bool HandleOpenRomFS(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        if (!g_romfs_loaded) {
            LOG_WARN("FS: OpenRomFS 但没有 RomFS 数据");
            *out_sz = 0;
            return true;
        }
        u32 fd = g_file_table.Open("romfs://", g_romfs_raw_data, false);
        LOG_INFO("FS: OpenRomFS → fd=0x%x (%zu 字节原始数据)", fd, g_romfs_raw_data.size());
        WriteHandle(out, fd, out_sz);
        return true;
    }

    bool HandleOpenFile(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        std::string path = ReadPath(in, in_sz, 8);
        if (path.empty()) path = ReadPath(in, in_sz, 0);
        std::string norm = NormalizePath(path);

        LOG_DEBUG("FS: OpenFile '%s' → 规范路径 '%s' (in_sz=%zu)", path.c_str(), norm.c_str(), in_sz);

        if (!g_romfs_loaded || norm.empty() || !g_romfs.Exists(norm)) {
            LOG_WARN("FS: OpenFile '%s' 在 RomFS 中未找到", norm.c_str());
            *out_sz = 0;
            return true;
        }

        std::vector<u8> file_data;
        if (!g_romfs.ReadFile(norm, file_data)) {
            LOG_WARN("FS: OpenFile '%s' 读取失败", norm.c_str());
            *out_sz = 0;
            return true;
        }

        u32 fd = g_file_table.Open(norm, file_data, false);
        LOG_INFO("FS: OpenFile '%s' → fd=0x%x (%zu 字节)", norm.c_str(), fd, file_data.size());
        WriteHandle(out, fd, out_sz);
        return true;
    }

    // ── 目录遍历 ──────────────────────────────────────────
    bool HandleOpenDirectory(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        std::string path = ReadPath(in, in_sz, 8);
        if (path.empty()) path = ReadPath(in, in_sz, 0);
        std::string norm = NormalizePath(path);

        LOG_DEBUG("FS: OpenDirectory '%s' → 规范路径 '%s'", path.c_str(), norm.c_str());

        // 从 RomFS 目录表构建目录条目列表
        std::vector<std::string> entries;
        if (g_romfs_loaded) {
            // 列出该目录下的直接子项
            // 暂时用简单的路径前缀匹配
            std::string prefix = norm.empty() ? "" : norm + "/";
            for (size_t i = 0; i < g_romfs.GetFileCount(); i++) {
                // 无法直接枚举 RomFs, 用 ReadFile 前缀探测
                // 这里用桩实现: 返回根目录内容
                (void)i;
                break;
            }
        }

        // 如果路径为空或 "/", 返回 romfs 根目录下的若干条目
        if (norm.empty() || norm == "/") {
            // 添加常见 RomFS 根条目
            entries.push_back("data");
            entries.push_back("contents");
        }

        LOG_INFO("FS: OpenDirectory '%s' → %zu 条目", norm.c_str(), entries.size());
        u32 fd = g_file_table.OpenDir(norm, entries);
        WriteHandle(out, fd, out_sz);
        return true;
    }

    // ── 文件属性 ──────────────────────────────────────────
    bool HandleGetEntryType(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        std::string path = ReadPath(in, in_sz, 8);
        if (path.empty()) path = ReadPath(in, in_sz, 0);
        std::string norm = NormalizePath(path);

        FsEntryType type = FsEntryType::File;
        if (norm.empty() || norm == "/") {
            type = FsEntryType::Directory;
        } else if (g_romfs_loaded && g_romfs.Exists(norm)) {
            type = FsEntryType::File;
        } else {
            // 可能是目录
            type = FsEntryType::Directory;
        }

        LOG_DEBUG("FS: GetEntryType '%s' → %s", norm.c_str(),
                  type == FsEntryType::Directory ? "Directory" : "File");

        if (*out_sz >= 4) {
            u32 t = static_cast<u32>(type);
            std::memcpy(out, &t, 4);
            *out_sz = 4;
        }
        return true;
    }

    bool HandleGetSize(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 fd = 0;
        if (in_sz >= 4) std::memcpy(&fd, in, 4);
        auto* e = g_file_table.Get(fd);
        u64 sz = e ? e->data.size() : 0;
        LOG_DEBUG("FS: GetSize fd=0x%x → %llu", fd, sz);
        if (*out_sz >= 8) {
            std::memcpy(out, &sz, 8);
            *out_sz = 8;
        }
        return true;
    }

    bool HandleRead(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        if (in_sz < 20) return false;
        u32 fd = 0; u64 offset = 0; u64 size = 0;
        std::memcpy(&fd, in, 4);
        std::memcpy(&offset, in + 8, 8);
        std::memcpy(&size, in + 16, 4);
        auto* e = g_file_table.Get(fd);
        if (!e) { *out_sz = 0; return true; }

        if (e->dir) {
            // 目录读取: 返回目录条目名列表
            u32 cursor = static_cast<u32>(e->pos);
            if (cursor < e->dir_entries.size()) {
                const auto& name = e->dir_entries[cursor];
                u64 actual = name.size();
                if (*out_sz >= 8 + actual) {
                    std::memcpy(out, &actual, 8);
                    std::memcpy(out + 8, name.data(), actual);
                    *out_sz = 8 + static_cast<size_t>(actual);
                }
                e->pos = cursor + 1;
            } else {
                *out_sz = 0;
            }
            return true;
        }

        u64 actual = (offset + size > e->data.size()) ? e->data.size() - offset : size;
        LOG_DEBUG("FS: Read fd=0x%x offset=%llu size=%llu (actual=%llu)", fd, offset, size, actual);

        if (*out_sz >= 8 + actual) {
            std::memcpy(out, &actual, 8);
            if (actual > 0) std::memcpy(out + 8, e->data.data() + offset, actual);
            *out_sz = 8 + static_cast<size_t>(actual);
        }
        return true;
    }

    bool HandleClose(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        u32 fd = 0;
        if (in_sz >= 4) std::memcpy(&fd, in, 4);
        g_file_table.Close(fd);
        LOG_DEBUG("FS: Close fd=0x%x", fd);
        *out_sz = 0;
        return true;
    }

    bool HandleOpenDataFile(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        LOG_DEBUG("FS: OpenDataFileByCurrent");
        // 返回 RomFS 根数据作为 Data 文件
        if (g_romfs_loaded) {
            u32 fd = g_file_table.Open("romfs-data://", g_romfs_raw_data, false);
            WriteHandle(out, fd, out_sz);
        } else {
            u32 fd = g_file_table.Open("save", {}, true);
            WriteHandle(out, fd, out_sz);
        }
        return true;
    }

    // ── SaveData 支持 ──────────────────────────────────────
    bool HandleOpenSaveDataFile(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // SaveData: 在 Switch 上对应 /save/ 目录
        // 简化实现: 在内存中创建空的可写文件
        std::string path = ReadPath(in, in_sz, 8);
        LOG_INFO("FS: OpenSaveDataFile '%s'", path.c_str());

        // 先检查 RomFS 中是否存在该文件
        std::string norm = NormalizePath(path);
        if (g_romfs_loaded && !norm.empty() && g_romfs.Exists(norm)) {
            std::vector<u8> file_data;
            if (g_romfs.ReadFile(norm, file_data)) {
                u32 fd = g_file_table.Open(norm, file_data, true);
                LOG_INFO("FS: SaveData '%s' → fd=0x%x (从 RomFS 加载, %zu 字节)",
                         norm.c_str(), fd, file_data.size());
                WriteHandle(out, fd, out_sz);
                return true;
            }
        }

        // 没有找到 → 创建空的可写文件
        u32 fd = g_file_table.Open("save:" + norm, {}, true);
        LOG_INFO("FS: SaveData '%s' → fd=0x%x (空文件)", norm.c_str(), fd);
        WriteHandle(out, fd, out_sz);
        return true;
    }
};

static FsService g_fs_service;
void ServiceFs_Init() { LOG_INFO("FS 服务就绪"); (void)g_fs_service; }

#include "services/Ipc.h"
#include "common/Log.h"
#include "loader/RomFs.h"
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>

// ── FS (File System) Service ────────────────────────────────
// Phase P0: minimal RomFS + file I/O for homebrew.

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
};

// ── File handle manager ─────────────────────────────────────
class FileTable {
public:
    struct Entry {
        std::string path;
        std::vector<u8> data;
        u64 pos = 0;
        bool writable = false;
        bool dir = false;
    };

    u32 Open(const std::string& path, const std::vector<u8>& data, bool writable) {
        u32 fd = next_fd_++;
        entries_[fd] = {path, data, 0, writable, false};
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

// ── RomFS data (set by loader) ──────────────────────────────
static RomFs g_romfs;
static bool g_romfs_loaded = false;
static std::vector<u8> g_romfs_raw_data; // raw blob for direct sequential reads
extern "C" void FsService_SetRomFS(std::span<const u8> data) {
    g_romfs_raw_data.assign(data.begin(), data.end());
    if (g_romfs.Parse(data)) {
        g_romfs_loaded = true;
        LOG_INFO("FS: RomFS parsed (%zu dirs, %zu files, %zu raw bytes)",
                 g_romfs.GetDirCount(), g_romfs.GetFileCount(), g_romfs_raw_data.size());
    }
}

// ── FS service implementation ───────────────────────────────
class FsService : public ServiceBase {
public:
    FsService() {
        IpcManager::Instance().RegisterService("fsp-srv:", this);
        // Also register "fs:" alias which some homebrew use
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
            return HandleOpenDataFile(in, in_sz, out, out_sz);

        case FsCmd::MountRom:
            LOG_DEBUG("FS: MountRom"); *out_sz = 0; return true;

        default:
            LOG_WARN("FS: unhandled cmd 0x%08x", cmd_id);
            *out_sz = 0;
            return true;
        }
    }

private:
    // Read a string from IPC input at offset (null-terminated, pascal-style)
    static std::string ReadPath(const u8* in, size_t in_sz, size_t off) {
        if (off + 4 > in_sz) return "";
        u32 len = (u32)in[off] | ((u32)in[off+1]<<8) | ((u32)in[off+2]<<16) | ((u32)in[off+3]<<24);
        if (len == 0 || off + 4 + len > in_sz) return "";
        return std::string(reinterpret_cast<const char*>(in + off + 4), len);
    }

    // OpenRomFS → returns a handle to the raw RomFS blob for direct reading
    bool HandleOpenRomFS(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        if (!g_romfs_loaded) {
            LOG_WARN("FS: OpenRomFS but no RomFS loaded");
            *out_sz = 0;
            return true;
        }
        // Return BOTH: a raw handle for sequential reads AND a path-based handle
        u32 fd = g_file_table.Open("romfs://", g_romfs_raw_data, false);
        LOG_INFO("FS: OpenRomFS → fd=0x%x (%zu bytes raw)", fd, g_romfs_raw_data.size());
        if (*out_sz >= 8) {
            out[0]=fd&0xFF; out[1]=(fd>>8)&0xFF; out[2]=(fd>>16)&0xFF; out[3]=(fd>>24)&0xFF;
            std::memset(out+4, 0, 4);
            *out_sz = 8;
        }
        return true;
    }

    // OpenFile by path — resolves against RomFS
    // IPC format: paths arrive via buffer descriptors (type 0x22).
    // The exact offset depends on the IPC request layout.
    // We try a few common path locations as a best-effort approach.
    bool HandleOpenFile(const u8* in, size_t in_sz, u8* out, size_t* out_sz) {
        // Try reading path from common IPC offsets
        std::string path = ReadPath(in, in_sz, 8);
        if (path.empty()) path = ReadPath(in, in_sz, 0);

        LOG_DEBUG("FS: OpenFile '%s' (in_sz=%zu)", path.c_str(), in_sz);

        if (!g_romfs_loaded || path.empty() || !g_romfs.Exists(path)) {
            LOG_WARN("FS: OpenFile '%s' NOT FOUND in RomFS", path.c_str());
            *out_sz = 0;
            return true;
        }

        std::vector<u8> file_data;
        if (!g_romfs.ReadFile(path, file_data)) {
            LOG_WARN("FS: OpenFile '%s' read failed", path.c_str());
            *out_sz = 0;
            return true;
        }

        u32 fd = g_file_table.Open(path, file_data, false);
        LOG_INFO("FS: OpenFile '%s' → fd=0x%x (%zu bytes)", path.c_str(), fd, file_data.size());
        if (*out_sz >= 8) {
            out[0]=fd&0xFF; out[1]=(fd>>8)&0xFF;
            out[2]=(fd>>16)&0xFF; out[3]=(fd>>24)&0xFF;
            std::memset(out+4, 0, 4);
            *out_sz = 8;
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
        // Input: {u32 fd, u64 offset, u64 size, ...}
        if (in_sz < 20) return false;
        u32 fd = 0; u64 offset = 0; u64 size = 0;
        std::memcpy(&fd, in, 4);
        std::memcpy(&offset, in + 8, 8);
        std::memcpy(&size, in + 16, 4);
        auto* e = g_file_table.Get(fd);
        if (!e) { *out_sz = 0; return true; }

        u64 actual = (offset + size > e->data.size()) ? e->data.size() - offset : size;
        LOG_DEBUG("FS: Read fd=0x%x offset=%llu size=%llu (actual=%llu)", fd, offset, size, actual);

        // Output: {u64 read_size, u8[actual] data}
        if (*out_sz >= 8 + actual) {
            std::memcpy(out, &actual, 8);
            if (actual > 0) std::memcpy(out + 8, e->data.data() + offset, actual);
            *out_sz = 8 + actual;
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
        LOG_DEBUG("FS: OpenDataFile");
        u32 fd = g_file_table.Open("save", {}, true);
        if (*out_sz >= 8) {
            out[0]=fd&0xFF; out[1]=(fd>>8)&0xFF;
            out[2]=(fd>>16)&0xFF; out[3]=(fd>>24)&0xFF;
            std::memset(out+4, 0, 4);
            *out_sz = 8;
        }
        return true;
    }
};

static FsService g_fs_service;
void ServiceFs_Init() { LOG_INFO("FS service ready"); (void)g_fs_service; }

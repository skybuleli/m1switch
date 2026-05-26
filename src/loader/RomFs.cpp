#include "loader/RomFs.h"
#include "common/Log.h"
#include <cstring>
#include <sstream>
#include <algorithm>

bool RomFs::Parse(std::span<const u8> data) {
    dir_table_.clear();
    file_table_.clear();
    romfs_data_ = data;

    u32 dir_table_off, dir_table_sz, file_table_off, file_table_sz;

    if (!ParseHeader(data, dir_table_off, dir_table_sz, file_table_off, file_table_sz, data_offset_))
        return false;

    if (!ParseDirectoryEntries(data, dir_table_off, dir_table_sz))
        return false;

    if (!ParseFileEntries(data, file_table_off, file_table_sz))
        return false;

    LOG_INFO("RomFS: %zu directories, %zu files, data @ 0x%x",
             dir_table_.size(), file_table_.size(), data_offset_);

    return true;
}

bool RomFs::ParseHeader(std::span<const u8> data,
                         u32& dir_table_off, u32& dir_table_size,
                         u32& file_table_off, u32& file_table_size,
                         u32& data_offset) {
    if (data.size() < 0x38) {
        LOG_WARN("RomFS: data too small for header (%zu bytes)", data.size());
        return false;
    }

    dir_table_off  = (u32)data[4]  | ((u32)data[5] << 8)  | ((u32)data[6] << 16)  | ((u32)data[7] << 24);
    dir_table_size = (u32)data[8]  | ((u32)data[9] << 8)  | ((u32)data[10] << 16) | ((u32)data[11] << 24);
    file_table_off = (u32)data[28] | ((u32)data[29] << 8) | ((u32)data[30] << 16) | ((u32)data[31] << 24);
    file_table_size= (u32)data[32] | ((u32)data[33] << 8) | ((u32)data[34] << 16) | ((u32)data[35] << 24);
    data_offset    = (u32)data[36] | ((u32)data[37] << 8) | ((u32)data[38] << 16) | ((u32)data[39] << 24);

    return true;
}

std::string RomFs::ReadName(std::span<const u8> data, u32 offset, u32 name_len) const {
    if (offset + name_len > data.size()) return "";
    return std::string(reinterpret_cast<const char*>(data.data() + offset), name_len);
}

bool RomFs::ParseDirectoryEntries(std::span<const u8> data, u32 table_off, u32 table_size) {
    if (table_off + table_size > data.size()) {
        LOG_WARN("RomFS: dir table out of bounds (off=0x%x sz=0x%x, data_sz=%zu)",
                 table_off, table_size, data.size());
        return false;
    }

    u32 pos = table_off;
    u32 end = table_off + table_size;

    while (pos + 16 <= end) {
        RomFsDirEntry entry;
        entry.parent_offset    = (u32)data[pos]    | ((u32)data[pos+1]  << 8) | ((u32)data[pos+2] << 16) | ((u32)data[pos+3] << 24);
        entry.sibling_offset   = (u32)data[pos+4]  | ((u32)data[pos+5]  << 8) | ((u32)data[pos+6] << 16) | ((u32)data[pos+7] << 24);
        entry.child_dir_offset = (u32)data[pos+8]  | ((u32)data[pos+9]  << 8) | ((u32)data[pos+10] << 16)| ((u32)data[pos+11] << 24);
        entry.child_file_offset= (u32)data[pos+12] | ((u32)data[pos+13] << 8) | ((u32)data[pos+14] << 16)| ((u32)data[pos+15] << 24);
        pos += 16;

        if (pos + 4 > end) break;
        entry.name_len = (u32)data[pos] | ((u32)data[pos+1] << 8) | ((u32)data[pos+2] << 16) | ((u32)data[pos+3] << 24);
        pos += 4;

        if (entry.name_len == 0 || pos + entry.name_len > end) break;
        entry.name = ReadName(data, pos, entry.name_len);
        pos += entry.name_len;

        dir_table_.push_back(entry);

        if (entry.name.empty() || entry.name == ".") continue;

        LOG_DEBUG("RomFS dir: '%s' parent=0x%x sibling=0x%x child=0x%x file=0x%x",
                  entry.name.c_str(), entry.parent_offset, entry.sibling_offset,
                  entry.child_dir_offset, entry.child_file_offset);
    }

    return true;
}

bool RomFs::ParseFileEntries(std::span<const u8> data, u32 table_off, u32 table_size) {
    if (table_off + table_size > data.size()) {
        LOG_WARN("RomFS: file table out of bounds");
        return false;
    }

    u32 pos = table_off;
    u32 end = table_off + table_size;

    while (pos + 20 <= end) {
        RomFsFileEntry entry;
        entry.parent_offset  = (u32)data[pos]    | ((u32)data[pos+1]  << 8) | ((u32)data[pos+2] << 16) | ((u32)data[pos+3] << 24);
        entry.sibling_offset = (u32)data[pos+4]  | ((u32)data[pos+5]  << 8) | ((u32)data[pos+6] << 16) | ((u32)data[pos+7] << 24);
        entry.data_offset    = (u64)data[pos+8]  | ((u64)data[pos+9]  << 8) | ((u64)data[pos+10] << 16) | ((u64)data[pos+11] << 24) | ((u64)data[pos+12] << 32) | ((u64)data[pos+13] << 40) | ((u64)data[pos+14] << 48) | ((u64)data[pos+15] << 56);
        entry.data_size      = (u64)data[pos+16] | ((u64)data[pos+17] << 8) | ((u64)data[pos+18] << 16) | ((u64)data[pos+19] << 24) | ((u64)data[pos+20] << 32) | ((u64)data[pos+21] << 40) | ((u64)data[pos+22] << 48) | ((u64)data[pos+23] << 56);
        pos += 24;

        if (pos + 4 > end) break;
        entry.name_len = (u32)data[pos] | ((u32)data[pos+1] << 8) | ((u32)data[pos+2] << 16) | ((u32)data[pos+3] << 24);
        pos += 4;

        if (entry.name_len == 0 || pos + entry.name_len > end) break;
        entry.name = ReadName(data, pos, entry.name_len);
        pos += entry.name_len;

        file_table_.push_back(entry);
    }

    return true;
}

std::string RomFs::BuildPath(u32 entry_index, bool is_file) const {
    if (is_file) {
        if (entry_index >= file_table_.size()) return "";
        auto& e = file_table_[entry_index];
        std::string path = e.name;

        u32 parent = e.parent_offset;
        u32 depth = 0;
        while (parent != 0xFFFFFFFF && depth < 100) {
            bool found = false;
            for (auto& d : dir_table_) {
                if (parent >= dir_table_.size()) break;
                const auto& dir = dir_table_[parent];
                if (dir.name != "." && !dir.name.empty()) {
                    path = dir.name + "/" + path;
                }
                parent = dir.parent_offset;
                found = true;
                break;
            }
            if (!found) break;
            depth++;
        }
        return path;
    } else {
        if (entry_index >= dir_table_.size()) return "";
        auto& e = dir_table_[entry_index];
        std::string path = e.name;

        u32 parent = e.parent_offset;
        u32 depth = 0;
        while (parent != 0xFFFFFFFF && depth < 100) {
            const auto& dir = dir_table_[parent];
            if (dir.name != "." && !dir.name.empty()) {
                path = dir.name + "/" + path;
            }
            parent = dir.parent_offset;
            depth++;
        }
        return path;
    }
}

bool RomFs::ReadFile(const std::string& path, std::vector<u8>& out_data) const {
    for (size_t i = 0; i < file_table_.size(); i++) {
        std::string fp = BuildPath((u32)i, true);
        if (fp == path) {
            u64 off = file_table_[i].data_offset + data_offset_;
            u64 sz  = file_table_[i].data_size;

            if (off + sz > romfs_data_.size()) {
                LOG_WARN("RomFS: file '%s' extends past data (off=0x%llx sz=%llu data_sz=%zu)",
                         path.c_str(), off, sz, romfs_data_.size());
                return false;
            }

            out_data.resize((size_t)sz);
            std::memcpy(out_data.data(), romfs_data_.data() + off, (size_t)sz);
            return true;
        }
    }
    return false;
}

bool RomFs::Exists(const std::string& path) const {
    for (size_t i = 0; i < file_table_.size(); i++) {
        if (BuildPath((u32)i, true) == path) return true;
    }
    for (size_t i = 0; i < dir_table_.size(); i++) {
        std::string dp = BuildPath((u32)i, false);
        if (dp == path || dp == path + "/") return true;
    }
    return false;
}

u64 RomFs::FileSize(const std::string& path) const {
    for (size_t i = 0; i < file_table_.size(); i++) {
        if (BuildPath((u32)i, true) == path) return file_table_[i].data_size;
    }
    return 0;
}

void RomFs::Dump() const {
    LOG_INFO("=== RomFS Contents ===");
    for (size_t i = 0; i < dir_table_.size(); i++) {
        std::string path = BuildPath((u32)i, false);
        if (!path.empty() && path != ".")
            LOG_INFO("  [DIR]  %s", path.c_str());
    }
    for (size_t i = 0; i < file_table_.size(); i++) {
        std::string path = BuildPath((u32)i, true);
        LOG_INFO("  [FILE] %s (%llu bytes)", path.c_str(), file_table_[i].data_size);
    }
}

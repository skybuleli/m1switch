#pragma once

#include "common/Types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <span>

struct RomFsDirEntry {
    u32 parent_offset;
    u32 sibling_offset;
    u32 child_dir_offset;
    u32 child_file_offset;
    u32 name_len;
    std::string name;
};

struct RomFsFileEntry {
    u32 parent_offset;
    u32 sibling_offset;
    u64 data_offset;
    u64 data_size;
    u32 name_len;
    std::string name;
};

class RomFs {
public:
    RomFs() = default;

    bool Parse(std::span<const u8> data);

    bool ReadFile(const std::string& path, std::vector<u8>& out_data) const;
    bool Exists(const std::string& path) const;
    u64 FileSize(const std::string& path) const;

    size_t GetFileCount() const { return file_table_.size(); }
    size_t GetDirCount() const { return dir_table_.size(); }

    void Dump() const;

private:
    bool ParseHeader(std::span<const u8> data, u32& dir_table_off, u32& dir_table_size,
                     u32& file_table_off, u32& file_table_size, u32& data_offset);
    bool ParseDirectoryEntries(std::span<const u8> data, u32 table_off, u32 table_size);
    bool ParseFileEntries(std::span<const u8> data, u32 table_off, u32 table_size);

    std::string ReadName(std::span<const u8> data, u32 offset, u32 name_len) const;
    std::string BuildPath(u32 entry_index, bool is_file) const;

    size_t FindFileByPath(const std::string& path) const;

    u32 data_offset_ = 0;
    std::vector<RomFsDirEntry> dir_table_;
    std::vector<RomFsFileEntry> file_table_;
    std::span<const u8> romfs_data_;
};

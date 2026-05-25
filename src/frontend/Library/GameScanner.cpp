#include "frontend/Library/GameScanner.h"
#include "common/Log.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

// ── NRO magic + NACP magic ─────────────────────────────────
constexpr u32 NRO0_MAGIC = 0x304F524E;  // "NRO0"
constexpr u32 NACP_MAGIC = 0x5043414E;  // "NACP"

GameScanner::GameScanner() {}

bool GameScanner::IsSupportedFile(const std::string& path) {
    auto ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".nro" || ext == ".xci" || ext == ".nsp";
}

// ── Extract metadata from NRO ───────────────────────────────
GameEntry GameScanner::ExtractMetadata(const std::string& path) {
    GameEntry entry;
    entry.path = path;
    entry.added_time = std::time(nullptr);

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return entry;

    auto fsize = static_cast<size_t>(file.tellg());
    entry.file_size = fsize;
    if (fsize < 0x200) { file.close(); return entry; }

    file.seekg(0);
    std::vector<u8> header(0x200);
    file.read(reinterpret_cast<char*>(header.data()), 0x200);
    file.close();

    auto read32 = [&](size_t off) -> u32 {
        if (off + 4 > header.size()) return 0;
        return (u32)header[off] | ((u32)header[off+1]<<8) |
               ((u32)header[off+2]<<16) | ((u32)header[off+3]<<24);
    };

    // Find NRO0 magic (usually at offset 0x10)
    u32 nro_off = 0;
    for (u32 i = 0; i + 4 <= header.size(); i += 4) {
        if (read32(i) == NRO0_MAGIC) { nro_off = i; break; }
    }

    if (nro_off == 0) return entry;  // Not an NRO

    // Read NRO0 header fields
    u32 text_start = read32(nro_off + 0x10);
    u32 text_size  = read32(nro_off + 0x14);
    u32 build_id_off = nro_off + 0x30;

    // Build ID
    char bid[65] = {};
    for (int i = 0; i < 0x20 && build_id_off + i < header.size(); i++)
        snprintf(bid + i * 2, 3, "%02x", header[build_id_off + i]);
    entry.build_id = bid;

    // Try to find NACP (icon + metadata) embedded in the NRO
    // NACP is usually at the end of the NRO, starting with "NACP" magic
    // For homebrew NROs, we search for NACP in the file
    // Phase 8: extract from NRO's embedded icon section

    // Set title from filename as fallback
    entry.title = fs::path(path).stem().string();

    return entry;
}

// ── Scan directory ──────────────────────────────────────────
size_t GameScanner::ScanDirectory(const std::string& dir, GameLibrary& lib) {
    if (!fs::is_directory(dir)) {
        LOG_WARN("Not a directory: %s", dir.c_str());
        return 0;
    }

    size_t count = 0;
    std::vector<std::string> files;

    // Collect files
    for (auto& entry : fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && IsSupportedFile(entry.path().string())) {
            files.push_back(entry.path().string());
        }
    }

    LOG_INFO("Scanning %s: %zu game files found", dir.c_str(), files.size());

    // Process with progress
    for (size_t i = 0; i < files.size(); i++) {
        if (progress_) progress_((int)i, (int)files.size(), files[i]);

        auto entry = ExtractMetadata(files[i]);
        if (!entry.title.empty()) {
            lib.AddOrUpdate(entry);
            count++;
        }
    }

    LOG_INFO("Scanned %zu games from %s", count, dir.c_str());
    return count;
}

size_t GameScanner::ScanDirectories(const std::vector<std::string>& dirs,
                                     GameLibrary& lib) {
    size_t total = 0;
    for (auto& d : dirs) total += ScanDirectory(d, lib);
    return total;
}

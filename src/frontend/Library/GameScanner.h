#pragma once

#include "frontend/Library/GameModel.h"
#include <functional>
#include <string>
#include <vector>

// ── Game Scanner ────────────────────────────────────────────
// Scans directories for Switch game files (NRO/XCI/NSP)
// and extracts metadata from NRO headers (NACP).

class GameScanner {
public:
    GameScanner();

    // Scan a directory recursively for game files
    // Returns number of games found
    size_t ScanDirectory(const std::string& dir, GameLibrary& lib);

    // Scan multiple directories
    size_t ScanDirectories(const std::vector<std::string>& dirs,
                           GameLibrary& lib);

    // Extract metadata from a single NRO file
    static GameEntry ExtractMetadata(const std::string& path);

    // Supported extensions
    static bool IsSupportedFile(const std::string& path);

    // Progress callback: (current, total, filename)
    using ProgressCallback = std::function<void(int, int, const std::string&)>;
    void SetProgressCallback(ProgressCallback cb) { progress_ = std::move(cb); }

private:
    ProgressCallback progress_;
};

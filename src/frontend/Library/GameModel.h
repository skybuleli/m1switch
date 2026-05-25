#pragma once

#include "common/Types.h"
#include <string>
#include <vector>
#include <chrono>

// ── Game entry in the library ───────────────────────────────
struct GameEntry {
    std::string path;          // Full file path
    std::string title;         // Display name
    std::string author;        // Developer/homebrew author
    std::string version;       // Version string
    std::string build_id;      // Build ID (hex)
    u64         file_size = 0; // File size in bytes
    u64         added_time = 0; // When added (epoch)
    u64         last_played = 0; // Last played (epoch)
    u32         play_count = 0;
    bool        is_favorite = false;
    bool        has_icon = false; // Has extracted icon

    // Serialization
    std::string ToJSON() const;
    static GameEntry FromJSON(const std::string& json);
};

// ── Game library model ─────────────────────────────────────
class GameLibrary {
public:
    GameLibrary();
    ~GameLibrary();

    // Add a game
    void AddOrUpdate(const GameEntry& entry);

    // Remove a game
    bool Remove(const std::string& path);

    // Get all games
    const std::vector<GameEntry>& GetAll() const { return games_; }

    // Find game by path
    GameEntry* Find(const std::string& path);

    // Search by title (case-insensitive substring)
    std::vector<const GameEntry*> Search(const std::string& query) const;

    // Sort options
    enum SortBy { Title, Added, LastPlayed, FileSize };
    void Sort(SortBy sort, bool ascending = true);

    // Toggle favorite
    void ToggleFavorite(const std::string& path);

    // Persistence
    bool Load(const std::string& path);
    bool Save(const std::string& path);

private:
    std::vector<GameEntry> games_;
};

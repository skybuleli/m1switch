#include "frontend/Library/GameModel.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>

// ── GameEntry serialization ─────────────────────────────────
std::string GameEntry::ToJSON() const {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"path\": \"" << path << "\",\n";
    ss << "  \"title\": \"" << title << "\",\n";
    ss << "  \"author\": \"" << author << "\",\n";
    ss << "  \"version\": \"" << version << "\",\n";
    ss << "  \"build_id\": \"" << build_id << "\",\n";
    ss << "  \"file_size\": " << file_size << ",\n";
    ss << "  \"added_time\": " << added_time << ",\n";
    ss << "  \"last_played\": " << last_played << ",\n";
    ss << "  \"play_count\": " << play_count << ",\n";
    ss << "  \"is_favorite\": " << (is_favorite ? "true" : "false") << ",\n";
    ss << "  \"has_icon\": " << (has_icon ? "true" : "false") << "\n";
    ss << "}";
    return ss.str();
}

GameEntry GameEntry::FromJSON(const std::string& json) {
    GameEntry e;
    auto find = [&](const std::string& key) -> std::string {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\"')) pos++;
        std::string val;
        while (pos < json.size() && json[pos] != '\"' && json[pos] != ',' && json[pos] != '\n' && json[pos] != '}') {
            val += json[pos++];
        }
        return val;
    };

    e.path = find("path");
    e.title = find("title");
    e.author = find("author");
    e.version = find("version");
    e.build_id = find("build_id");
    // 数值字段可能为空或损坏，用 try/catch 保护
    auto safe_stoull = [](const std::string& s) -> u64 {
        try { return s.empty() ? 0ULL : std::stoull(s, nullptr, 0); }
        catch (...) { return 0ULL; }
    };
    auto safe_stoul = [](const std::string& s) -> u32 {
        try { return s.empty() ? 0U : std::stoul(s, nullptr, 0); }
        catch (...) { return 0U; }
    };
    e.file_size = safe_stoull(find("file_size"));
    e.added_time = safe_stoull(find("added_time"));
    e.last_played = safe_stoull(find("last_played"));
    e.play_count = safe_stoul(find("play_count"));
    e.is_favorite = find("is_favorite") == "true";
    e.has_icon = find("has_icon") == "true";

    if (e.title.empty()) e.title = e.path;
    return e;
}

// ── GameLibrary ────────────────────────────────────────────
GameLibrary::GameLibrary() {}
GameLibrary::~GameLibrary() {}

void GameLibrary::AddOrUpdate(const GameEntry& entry) {
    for (auto& g : games_) {
        if (g.path == entry.path) {
            g = entry;
            return;
        }
    }
    games_.push_back(entry);
}

bool GameLibrary::Remove(const std::string& path) {
    for (auto it = games_.begin(); it != games_.end(); ++it) {
        if (it->path == path) { games_.erase(it); return true; }
    }
    return false;
}

GameEntry* GameLibrary::Find(const std::string& path) {
    for (auto& g : games_) if (g.path == path) return &g;
    return nullptr;
}

std::vector<const GameEntry*> GameLibrary::Search(const std::string& query) const {
    std::vector<const GameEntry*> results;
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    for (auto& g : games_) {
        std::string title = g.title;
        std::transform(title.begin(), title.end(), title.begin(), ::tolower);
        if (title.find(q) != std::string::npos)
            results.push_back(&g);
    }
    return results;
}

void GameLibrary::Sort(SortBy sort, bool ascending) {
    std::sort(games_.begin(), games_.end(),
        [sort, ascending](const GameEntry& a, const GameEntry& b) {
            int cmp = 0;
            switch (sort) {
            case Title:      cmp = a.title.compare(b.title); break;
            case Added:      cmp = (a.added_time < b.added_time) ? -1 : (a.added_time > b.added_time); break;
            case LastPlayed: cmp = (a.last_played < b.last_played) ? -1 : (a.last_played > b.last_played); break;
            case FileSize:   cmp = (a.file_size < b.file_size) ? -1 : (a.file_size > b.file_size); break;
            }
            return ascending ? cmp < 0 : cmp > 0;
        });
}

void GameLibrary::ToggleFavorite(const std::string& path) {
    auto* g = Find(path);
    if (g) g->is_favorite = !g->is_favorite;
}

bool GameLibrary::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    std::string json;
    while (std::getline(file, line)) json += line + "\n";
    file.close();

    // Simple JSON array parsing
    auto pos = json.find('[');
    if (pos == std::string::npos) return false;

    games_.clear();
    int depth = 0;
    std::string entry;
    for (size_t i = pos + 1; i < json.size(); i++) {
        if (json[i] == '{') depth++;
        if (json[i] == '}') depth--;
        entry += json[i];
        if (depth == 0 && !entry.empty()) {
            // Parse entry
            games_.push_back(GameEntry::FromJSON(entry));
            entry.clear();
            // Skip to next entry
            while (i < json.size() && json[i] != '{' && json[i] != ']') i++;
            if (i < json.size() && json[i] == '{') i--;  // re-process
        }
    }

    return true;
}

bool GameLibrary::Save(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "[\n";
    for (size_t i = 0; i < games_.size(); i++) {
        file << games_[i].ToJSON();
        if (i + 1 < games_.size()) file << ",";
        file << "\n";
    }
    file << "]\n";
    file.close();
    return true;
}

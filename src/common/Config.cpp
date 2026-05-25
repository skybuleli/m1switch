#include "Config.h"
#include "Log.h"

#include <fstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

Config& Config::Instance() {
    static Config instance;
    return instance;
}

fs::path Config::DataDir() const {
    // macOS standard location
    const char* home = getenv("HOME");
    if (!home) {
        return fs::path("/tmp/m1switch");
    }
    return fs::path(home) / "Library" / "Application Support" / "m1switch";
}

std::string Config::ConfigFilePath() const {
    return (DataDir() / "config.json").string();
}

Result Config::Load() {
    auto path = ConfigFilePath();
    LOG_INFO("Loading config from %s", path.c_str());

    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("No config file found, using defaults");
        return Result::Success; // Not an error, will create on Save
    }

    // Phase 0: naive JSON parsing
    // Will be replaced with nlohmann-json in Phase 0.2
    LOG_WARN("JSON parser not yet integrated, using defaults");
    file.close();
    return Result::Success;
}

Result Config::Save() {
    auto path = ConfigFilePath();
    fs::create_directories(DataDir());

    LOG_INFO("Saving config to %s", path.c_str());

    // Phase 0: write a placeholder JSON
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to write config to %s", path.c_str());
        return Result::PermissionDenied;
    }

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"gpu\": {\n";
    file << "    \"resolution_scale\": " << gpu_.resolution_scale << ",\n";
    file << "    \"anisotropy\": " << gpu_.anisotropy << ",\n";
    file << "    \"vsync\": " << (gpu_.vsync ? "true" : "false") << ",\n";
    file << "    \"msaa\": " << gpu_.msaa << ",\n";
    file << "    \"gpu_accuracy\": " << (gpu_.gpu_accuracy ? "true" : "false") << "\n";
    file << "  },\n";
    file << "  \"audio\": {\n";
    file << "    \"output_device\": \"" << audio_.output_device << "\",\n";
    file << "    \"volume\": " << audio_.volume << "\n";
    file << "  },\n";
    file << "  \"system\": {\n";
    file << "    \"language\": \"" << system_.language << "\",\n";
    file << "    \"timezone\": \"" << system_.timezone << "\",\n";
    file << "    \"user_name\": \"" << system_.user_name << "\"\n";
    file << "  },\n";
    file << "  \"paths\": {\n";
    file << "    \"game_dirs\": \"" << path_.game_dirs << "\"\n";
    file << "  }\n";
    file << "}\n";

    file.close();
    LOG_INFO("Config saved");
    return Result::Success;
}

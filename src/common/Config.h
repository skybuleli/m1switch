#pragma once

#include "Types.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <filesystem>
#include <optional>

// ── Configuration system ─────────────────────────────────────
// Phase 0: JSON-based config file in ~/Library/Application Support/m1switch/
// Will be replaced by a more structured system later.

struct GpuConfig {
    f32 resolution_scale = 1.0f;     // 1.0x = native
    u32 anisotropy = 4;               // 0=off, 2/4/8/16
    bool vsync = true;
    u32 msaa = 1;                     // 1/2/4/8
    bool gpu_accuracy = true;         // true=precise, false=fast
};

struct AudioConfig {
    std::string output_device;        // empty = system default
    f32 volume = 1.0f;
};

struct InputConfig {
    std::unordered_map<std::string, std::string> key_bindings;
};

struct SystemConfig {
    std::string language = "en";
    std::string timezone = "UTC";
    std::string user_name = "M1Switch";
};

struct PathConfig {
    std::string firmware_dir;
    std::string keys_file;
    std::string save_data_dir;
    std::string shader_cache_dir;
    std::string game_dirs;            // colon-separated list
};

class Config {
public:
    static Config& Instance();

    Result Load();
    Result Save();

    // ── Getters ────────────────────────────────────────
    const GpuConfig& Gpu() const { return gpu_; }
    const AudioConfig& Audio() const { return audio_; }
    const InputConfig& Input() const { return input_; }
    const SystemConfig& System() const { return system_; }
    const PathConfig& Paths() const { return path_; }

    // ── Setters ────────────────────────────────────────
    GpuConfig& Gpu() { return gpu_; }
    AudioConfig& Audio() { return audio_; }
    InputConfig& Input() { return input_; }
    SystemConfig& System() { return system_; }
    PathConfig& Paths() { return path_; }

    // ── Paths ──────────────────────────────────────────
    std::filesystem::path DataDir() const;
    std::string ConfigFilePath() const;

private:
    Config() = default;

    GpuConfig gpu_;
    AudioConfig audio_;
    InputConfig input_;
    SystemConfig system_;
    PathConfig path_;
};

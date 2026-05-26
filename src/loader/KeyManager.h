#pragma once

#include "common/Types.h"
#include "common/Log.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

class KeyManager {
public:
    static KeyManager& Instance();

    bool LoadFromFile(const std::string& path);
    bool LoadFromDefaultPaths();

    bool HasKey(const std::string& name) const;
    std::vector<u8> GetKey(const std::string& name) const;
    u64 GetKeyRevision() const { return revision_; }

    void SetKey(const std::string& name, const std::vector<u8>& key);

    bool HasHeaderKey() const { return HasKey("header_key"); }
    std::vector<u8> GetHeaderKey() const { return GetKey("header_key"); }

    std::vector<u8> GetKeyAreaKey(u8 index, u8 generation) const;

private:
    KeyManager() = default;

    std::unordered_map<std::string, std::vector<u8>> keys_;
    u64 revision_ = 0;

    static std::vector<std::string> GetSearchPaths();
    static std::vector<u8> ParseHex(const std::string& hex);
};

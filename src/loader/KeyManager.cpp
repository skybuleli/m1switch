#include "loader/KeyManager.h"
#include <fstream>
#include <sstream>
#include <cctype>

KeyManager& KeyManager::Instance() {
    static KeyManager km;
    return km;
}

std::vector<u8> KeyManager::ParseHex(const std::string& hex) {
    std::vector<u8> bytes;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        auto high = std::isxdigit(hex[i]) ? (hex[i] >= 'a' ? hex[i] - 'a' + 10 : hex[i] >= 'A' ? hex[i] - 'A' + 10 : hex[i] - '0') : -1;
        auto low  = std::isxdigit(hex[i+1]) ? (hex[i+1] >= 'a' ? hex[i+1] - 'a' + 10 : hex[i+1] >= 'A' ? hex[i+1] - 'A' + 10 : hex[i+1] - '0') : -1;
        if (high < 0 || low < 0) continue;
        bytes.push_back((u8)((high << 4) | low));
    }
    return bytes;
}

bool KeyManager::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("KeyManager: cannot open %s", path.c_str());
        return false;
    }

    std::string line;
    u32 loaded = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string name = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        while (!name.empty() && std::isspace(name.back())) name.pop_back();
        while (!value.empty() && std::isspace(value.back())) value.pop_back();
        while (!value.empty() && std::isspace(value.front())) value = value.substr(1);

        if (value.length() >= 32) {
            auto key = ParseHex(value);
            if (key.size() == 16) {
                keys_[name] = key;
                loaded++;
            }
        }
    }

    LOG_INFO("KeyManager: loaded %u keys from %s", loaded, path.c_str());
    return loaded > 0;
}

std::vector<std::string> KeyManager::GetSearchPaths() {
    std::vector<std::string> paths;
    const char* home = getenv("HOME");
    if (home) {
        paths.push_back(std::string(home) + "/.switch/prod.keys");
        paths.push_back(std::string(home) + "/.switch/keys.txt");
    }
    paths.push_back("prod.keys");
    paths.push_back("keys.txt");
    return paths;
}

bool KeyManager::LoadFromDefaultPaths() {
    for (const auto& p : GetSearchPaths()) {
        if (LoadFromFile(p)) {
            if (HasHeaderKey() || HasKey("key_area_key_application_00")) {
                LOG_INFO("KeyManager: valid keys found in %s", p.c_str());
                return true;
            }
        }
    }
    LOG_WARN("KeyManager: no keys found in default paths");
    return false;
}

bool KeyManager::HasKey(const std::string& name) const {
    return keys_.find(name) != keys_.end();
}

std::vector<u8> KeyManager::GetKey(const std::string& name) const {
    auto it = keys_.find(name);
    if (it != keys_.end()) return it->second;
    return {};
}

void KeyManager::SetKey(const std::string& name, const std::vector<u8>& key) {
    keys_[name] = key;
}

std::vector<u8> KeyManager::GetKeyAreaKey(u8 index, u8 generation) const {
    static const char* key_names[] = {
        "key_area_key_application_00",
        "key_area_key_application_01",
        "key_area_key_application_02",
        "key_area_key_application_03",
    };

    if (index < 4) {
        std::string name = std::string(key_names[index]);
        if (generation > 0)
            name += "_" + std::to_string(generation);
        if (HasKey(name)) return GetKey(name);
    }

    char genkey[64];
    snprintf(genkey, sizeof(genkey), "key_area_key_application_%02x", index);
    std::string gname(genkey);
    if (generation > 0)
        gname += "_" + std::to_string(generation);
    if (HasKey(gname)) return GetKey(gname);

    return {};
}

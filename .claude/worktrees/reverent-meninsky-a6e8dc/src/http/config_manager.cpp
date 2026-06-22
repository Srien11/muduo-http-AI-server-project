#include "http/config_manager.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace muduo_http {

bool ConfigManager::Load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[config] cannot open: " << filepath << '\n';
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadString(buffer.str());
}

bool ConfigManager::LoadString(const std::string& content) {
    config_.clear();

    std::istringstream stream(content);
    std::string line;
    std::string current_section;

    while (std::getline(stream, line)) {
        // Trim
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip empty and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        // Section header [section]
        if (line[0] == '[') {
            auto end = line.find(']');
            if (end != std::string::npos) {
                current_section = line.substr(1, end - 1);
            }
            continue;
        }

        // Key = value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Trim spaces
        auto trim = [](std::string& s) {
            while (!s.empty() && s.front() == ' ') s.erase(0, 1);
            while (!s.empty() && s.back() == ' ') s.pop_back();
        };
        trim(key);
        trim(value);

        // Store as "section.key" or just "key"
        std::string full_key = current_section.empty() ? key : current_section + "." + key;
        config_[full_key] = value;
    }

    return true;
}

std::string ConfigManager::Get(const std::string& key, const std::string& default_val) const {
    auto it = config_.find(key);
    if (it != config_.end()) return it->second;
    return default_val;
}

int ConfigManager::GetInt(const std::string& key, int default_val) const {
    auto it = config_.find(key);
    if (it == config_.end()) return default_val;
    try { return std::stoi(it->second); } catch (...) { return default_val; }
}

bool ConfigManager::GetBool(const std::string& key, bool default_val) const {
    auto val = Get(key, "");
    if (val.empty()) return default_val;
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    return val == "true" || val == "1" || val == "yes";
}

bool ConfigManager::Has(const std::string& key) const {
    return config_.find(key) != config_.end();
}

} // namespace muduo_http

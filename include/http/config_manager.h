#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace muduo_http {

// ConfigManager: load key-value config from file
// Format:
//   # comment
//   key = value
//   [section]
//   key = value
//
// Access: Get("key") or Get("section.key")
class ConfigManager {
public:
    ConfigManager() = default;

    bool Load(const std::string& filepath);
    bool LoadString(const std::string& content);

    std::string Get(const std::string& key, const std::string& default_val = "") const;
    int GetInt(const std::string& key, int default_val = 0) const;
    bool GetBool(const std::string& key, bool default_val = false) const;

    bool Has(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> config_;
};

} // namespace muduo_http

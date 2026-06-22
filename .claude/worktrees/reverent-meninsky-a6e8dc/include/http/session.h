#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "muduo/base/Timestamp.h"

namespace muduo_http {

class Session {
public:
    explicit Session(std::string id);

    void Set(const std::string& key, const std::string& value);
    std::string Get(const std::string& key) const;
    bool Has(const std::string& key) const;
    void Remove(const std::string& key);
    void Clear();

    const std::string& id() const { return id_; }
    bool expired(int timeout_seconds) const;
    void Refresh();

private:
    std::string id_;
    std::unordered_map<std::string, std::string> data_;
    muduo::Timestamp created_at_;
    muduo::Timestamp last_access_;
};

class SessionManager {
public:
    explicit SessionManager(int timeout_seconds = 3600);

    std::shared_ptr<Session> Create();
    std::shared_ptr<Session> Get(const std::string& id);
    void Destroy(const std::string& id);
    void Cleanup();

    int timeout_seconds() const { return timeout_seconds_; }

private:
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
    int timeout_seconds_;
};

std::string GenerateSessionId();

} // namespace muduo_http

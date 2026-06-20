#include "http/session.h"

#include <chrono>
#include <cstdio>
#include <random>

namespace muduo_http {

// -------- Session --------

Session::Session(std::string id)
    : id_(std::move(id)),
      created_at_(muduo::Timestamp::now()),
      last_access_(created_at_) {}

void Session::Set(const std::string& key, const std::string& value) {
    data_[key] = value;
    last_access_ = muduo::Timestamp::now();
}

std::string Session::Get(const std::string& key) const {
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return "";
}

bool Session::Has(const std::string& key) const {
    return data_.find(key) != data_.end();
}

void Session::Remove(const std::string& key) {
    data_.erase(key);
    last_access_ = muduo::Timestamp::now();
}

void Session::Clear() {
    data_.clear();
    last_access_ = muduo::Timestamp::now();
}

bool Session::expired(int timeout_seconds) const {
    const auto now = muduo::Timestamp::now();
    const double elapsed = muduo::timeDifference(now, last_access_);
    return elapsed >= timeout_seconds;
}

void Session::Refresh() {
    last_access_ = muduo::Timestamp::now();
}

// -------- SessionManager --------

SessionManager::SessionManager(int timeout_seconds)
    : timeout_seconds_(timeout_seconds) {}

std::shared_ptr<Session> SessionManager::Create() {
    auto session = std::make_shared<Session>(GenerateSessionId());
    sessions_[session->id()] = session;
    return session;
}

std::shared_ptr<Session> SessionManager::Get(const std::string& id) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) {
        return nullptr;
    }

    auto session = it->second;
    if (session->expired(timeout_seconds_)) {
        sessions_.erase(it);
        return nullptr;
    }

    session->Refresh();
    return session;
}

void SessionManager::Destroy(const std::string& id) {
    sessions_.erase(id);
}

void SessionManager::Cleanup() {
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (it->second->expired(timeout_seconds_)) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

// -------- Session ID Generator --------

std::string GenerateSessionId() {
    // 32 hex chars = 128 bits of randomness
    std::string hex(32, '0');
    std::random_device rd;
    for (size_t i = 0; i < 16; ++i) {
        const unsigned char byte = static_cast<unsigned char>(rd());
        std::sprintf(&hex[i * 2], "%02x", byte);
    }
    return hex;
}

} // namespace muduo_http

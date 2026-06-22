#pragma once

#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <openssl/sha.h>

namespace muduo_http {

// Simple file-based user auth system
class UserAuth {
public:
    UserAuth(const std::string& db_path = "users.json",
             const std::string& sess_path = "sessions.json")
        : db_path_(db_path), sess_path_(sess_path) {
        Load();
        CleanExpired();
    }

    // Register: returns error string (empty = success)
    std::string Register(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (username.empty() || password.empty()) return "用户名和密码不能为空";
        if (username.size() < 2) return "用户名至少2个字符";
        if (password.size() < 4) return "密码至少4个字符";
        if (users_.find(username) != users_.end()) return "用户名已存在";
        User u;
        u.password_hash = Hash(password);
        u.created_at = time(nullptr);
        users_[username] = u;
        Save();
        return "";
    }

    // Login: returns session token (empty = failed)
    std::string Login(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(username);
        if (it == users_.end()) return "";
        if (it->second.password_hash != Hash(password)) return "";

        // Create session token
        std::string token = username + "_" + std::to_string(time(nullptr)) + "_" + std::to_string(rand());
        SHA256(reinterpret_cast<const unsigned char*>(token.data()), token.size(),
               reinterpret_cast<unsigned char*>(&token[0]));
        // Actually let me just use a simple random token
        token = RandomToken();
        Session s;
        s.username = username;
        s.expires_at = time(nullptr) + 86400 * 7; // 7 days
        sessions_[token] = s;
        SaveSessions();
        return token;
    }

    // Validate token: returns username (empty = invalid)
    std::string Validate(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(token);
        if (it == sessions_.end()) return "";
        if (it->second.expires_at < time(nullptr)) {
            sessions_.erase(it);
            SaveSessions();
            return "";
        }
        return it->second.username;
    }

    void Logout(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(token);
        SaveSessions();
    }

private:
    struct User {
        std::string password_hash;
        time_t created_at = 0;
    };
    struct Session {
        std::string username;
        time_t expires_at = 0;
    };

    std::string Hash(const std::string& s) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), hash);
        std::ostringstream oss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
            oss << std::hex << (int)hash[i];
        return oss.str();
    }

    std::string RandomToken() {
        std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
        std::string token;
        srand(time(nullptr) ^ clock());
        for (int i = 0; i < 32; i++)
            token += chars[rand() % chars.size()];
        return token;
    }

    void Load() {
        std::ifstream f(db_path_);
        if (!f.is_open()) return;
        nlohmann::json j;
        try { f >> j; } catch (...) { return; }
        for (auto& [key, val] : j.items()) {
            User u;
            u.password_hash = val.value("password_hash", "");
            u.created_at = val.value("created_at", 0L);
            users_[key] = u;
        }
        std::ifstream sf(sess_path_);
        if (!sf.is_open()) return;
        try { sf >> j; } catch (...) { return; }
        for (auto& [key, val] : j.items()) {
            Session s;
            s.username = val.value("username", "");
            s.expires_at = val.value("expires_at", 0L);
            sessions_[key] = s;
        }
    }

    void Save() {
        nlohmann::json j;
        for (auto& [name, u] : users_)
            j[name] = {{"password_hash", u.password_hash}, {"created_at", u.created_at}};
        std::ofstream f(db_path_);
        f << j.dump(2);
    }

    void SaveSessions() {
        nlohmann::json j;
        for (auto& [token, s] : sessions_)
            j[token] = {{"username", s.username}, {"expires_at", s.expires_at}};
        std::ofstream f(sess_path_);
        f << j.dump(2);
    }

    void CleanExpired() {
        auto now = time(nullptr);
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->second.expires_at < now) it = sessions_.erase(it);
            else ++it;
        }
    }

    std::string db_path_;
    std::string sess_path_;
    std::map<std::string, User> users_;
    std::map<std::string, Session> sessions_;
    std::mutex mutex_;
};

} // namespace muduo_http

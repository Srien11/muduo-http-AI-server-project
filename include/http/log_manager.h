#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <fstream>

namespace muduo_http {

enum class LogLevel {
    kDebug = 0,
    kInfo = 1,
    kWarn = 2,
    kError = 3,
    kNone = 4,
};

class LogManager {
public:
    static LogManager& Instance();

    void SetLevel(LogLevel level);
    void SetFile(const std::string& filepath);
    void SetConsole(bool enabled);

    void Log(LogLevel level, const std::string& file, int line,
             const std::string& func, const std::string& msg);

    // Convenience macros call these
    static const char* LevelName(LogLevel level);

    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

private:
    LogManager() = default;
    ~LogManager();

    std::string FormatTimestamp() const;

    LogLevel level_{LogLevel::kInfo};
    std::unique_ptr<std::ofstream> file_;
    bool console_{true};
    std::mutex mutex_;
};

// Convenience macros
#define LOG_DEBUG(msg)  muduo_http::LogManager::Instance().Log(muduo_http::LogLevel::kDebug, __FILE__, __LINE__, __func__, msg)
#define LOG_INFO(msg)   muduo_http::LogManager::Instance().Log(muduo_http::LogLevel::kInfo, __FILE__, __LINE__, __func__, msg)
#define LOG_WARN(msg)   muduo_http::LogManager::Instance().Log(muduo_http::LogLevel::kWarn, __FILE__, __LINE__, __func__, msg)
#define LOG_ERROR(msg)  muduo_http::LogManager::Instance().Log(muduo_http::LogLevel::kError, __FILE__, __LINE__, __func__, msg)

} // namespace muduo_http

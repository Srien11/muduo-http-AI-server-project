#include "http/log_manager.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace muduo_http {

LogManager& LogManager::Instance() {
    static LogManager instance;
    return instance;
}

LogManager::~LogManager() {
    if (file_ && file_->is_open()) {
        file_->close();
    }
}

void LogManager::SetLevel(LogLevel level) {
    level_ = level;
}

void LogManager::SetFile(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto new_file = std::make_unique<std::ofstream>(filepath, std::ios::app);
    if (new_file->is_open()) {
        file_ = std::move(new_file);
    } else {
        std::cerr << "[log] cannot open: " << filepath << '\n';
    }
}

void LogManager::SetConsole(bool enabled) {
    console_ = enabled;
}

void LogManager::Log(LogLevel level, const std::string& file, int line,
                      const std::string& func, const std::string& msg) {
    if (level < level_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream entry;
    entry << "[" << FormatTimestamp() << "]"
          << "[" << LevelName(level) << "]"
          << "[" << file << ":" << line << "]"
          << " " << msg;

    std::string formatted = entry.str();

    if (console_) {
        std::cout << formatted << std::endl;
    }

    if (file_ && file_->is_open()) {
        *file_ << formatted << std::endl;
    }
}

const char* LogManager::LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug: return "DEBUG";
        case LogLevel::kInfo:  return "INFO";
        case LogLevel::kWarn:  return "WARN";
        case LogLevel::kError: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string LogManager::FormatTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_r(&tt, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace muduo_http

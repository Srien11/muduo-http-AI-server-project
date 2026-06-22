#include "http/db_connection_pool.h"

#include <iostream>
#include <sstream>
#include <utility>

namespace muduo_http {

// -------- DbConnection --------

DbConnection::DbConnection() = default;

DbConnection::~DbConnection() {
    Disconnect();
}

bool DbConnection::Connect(const DbConfig& config) {
    if (conn_) {
        Disconnect();
    }

    conn_ = mysql_init(nullptr);
    if (!conn_) {
        std::cerr << "[db] mysql_init failed\n";
        return false;
    }

    unsigned int timeout = static_cast<unsigned int>(config.connect_timeout);
    mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    MYSQL* result = mysql_real_connect(conn_, config.host.c_str(), config.user.c_str(),
                                        config.password.c_str(), config.database.empty() ? nullptr : config.database.c_str(),
                                        config.port, nullptr, 0);
    if (!result) {
        std::cerr << "[db] connect failed: " << mysql_error(conn_) << '\n';
        mysql_close(conn_);
        conn_ = nullptr;
        return false;
    }

    // Use UTF-8
    mysql_set_character_set(conn_, "utf8mb4");
    return true;
}

void DbConnection::Disconnect() {
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool DbConnection::IsConnected() const {
    return conn_ != nullptr;
}

bool DbConnection::Ping() {
    if (!conn_) return false;
    return mysql_ping(conn_) == 0;
}

DbConnection::DbConnection(DbConnection&& other) noexcept
    : conn_(other.conn_) {
    other.conn_ = nullptr;
}

DbConnection& DbConnection::operator=(DbConnection&& other) noexcept {
    if (this != &other) {
        Disconnect();
        conn_ = other.conn_;
        other.conn_ = nullptr;
    }
    return *this;
}

// -------- DbConnectionPool --------

DbConnectionPool::DbConnectionPool(DbConfig config)
    : config_(std::move(config)) {}

DbConnectionPool::~DbConnectionPool() {
    Drain();
}

std::shared_ptr<DbConnection> DbConnectionPool::Get() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Try idle pool first
    while (!idle_.empty()) {
        auto conn = idle_.front();
        idle_.pop();
        if (conn->Ping()) {
            ++active_count_;
            return conn;
        }
        // Dead connection, discard
    }

    // Create new connection
    if (active_count_ + static_cast<int>(idle_.size()) >= config_.max_connections) {
        std::cerr << "[db] max connections reached (" << config_.max_connections << ")\n";
        return nullptr;
    }

    auto conn = std::make_shared<DbConnection>();
    if (!conn->Connect(config_)) {
        return nullptr;
    }

    ++active_count_;
    return conn;
}

void DbConnectionPool::Return(std::shared_ptr<DbConnection> conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(mutex_);
    --active_count_;
    if (conn->Ping()) {
        idle_.push(std::move(conn));
    }
}

void DbConnectionPool::Drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!idle_.empty()) {
        idle_.pop();
    }
    active_count_ = 0;
}

int DbConnectionPool::idle_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(idle_.size());
}

int DbConnectionPool::active_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_count_;
}

} // namespace muduo_http

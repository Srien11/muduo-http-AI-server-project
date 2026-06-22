#pragma once

#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <mysql/mysql.h>

namespace muduo_http {

struct DbConfig {
    std::string host{"127.0.0.1"};
    int port{3306};
    std::string user{"root"};
    std::string password;
    std::string database;
    int connect_timeout{5};
    int max_connections{10};
};

class DbConnection {
public:
    DbConnection();
    ~DbConnection();

    bool Connect(const DbConfig& config);
    void Disconnect();
    bool IsConnected() const;
    bool Ping();

    MYSQL* raw() { return conn_; }

    DbConnection(const DbConnection&) = delete;
    DbConnection& operator=(const DbConnection&) = delete;

    DbConnection(DbConnection&& other) noexcept;
    DbConnection& operator=(DbConnection&& other) noexcept;

private:
    MYSQL* conn_{nullptr};
};

class DbConnectionPool {
public:
    explicit DbConnectionPool(DbConfig config);
    ~DbConnectionPool();

    std::shared_ptr<DbConnection> Get();
    void Return(std::shared_ptr<DbConnection> conn);
    void Drain();

    int idle_count() const;
    int active_count() const;

private:
    DbConfig config_;
    std::queue<std::shared_ptr<DbConnection>> idle_;
    int active_count_{0};
    mutable std::mutex mutex_;
};

} // namespace muduo_http

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "muduo/net/EventLoop.h"

namespace muduo_http {

class GracefulShutdown {
public:
    using ShutdownCallback = std::function<void()>;

    static GracefulShutdown& Instance();

    // Register a cleanup callback (e.g. stop server, drain connection pool)
    void Register(ShutdownCallback callback);

    // Install signal handlers (SIGINT, SIGTERM)
    void Install(muduo::net::EventLoop* loop);

    // Trigger shutdown manually
    void Shutdown();

    GracefulShutdown(const GracefulShutdown&) = delete;
    GracefulShutdown& operator=(const GracefulShutdown&) = delete;

private:
    GracefulShutdown() = default;

    std::vector<ShutdownCallback> callbacks_;
    bool shutting_down_{false};
};

} // namespace muduo_http

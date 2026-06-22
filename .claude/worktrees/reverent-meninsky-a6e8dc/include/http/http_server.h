#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <chrono>

#include "muduo/net/TcpServer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/TcpConnection.h"

#include "http/http_request.h"
#include "http/middleware.h"
#include "http/router.h"
#include "http/session.h"

namespace muduo_http {

class HttpServer {
public:
    explicit HttpServer(int port);
    ~HttpServer() = default;

    void Start();

    Router& routes();
    void Use(Middleware middleware);

    // Configuration
    void set_max_body_size(size_t bytes) { max_body_size_ = bytes; }
    size_t max_body_size() const { return max_body_size_; }

    // Thread pool: spread connections across N threads to prevent blocking
    // Default 1 (single-threaded). Set to e.g. 4 for AI workloads.
    void set_thread_num(int n) { thread_num_ = n; }
    int thread_num() const { return thread_num_; }

    // Session
    std::shared_ptr<SessionManager> session_manager() const { return session_manager_; }
    void set_session_timeout(int seconds);

    // Access event loop (for graceful shutdown)
    muduo::net::EventLoop* get_loop() { return &loop_; }

    // Stats
    void IncrementRequests() { ++request_count_; }
    void IncrementResponses() { ++response_count_; }
    void TrackResponseTime(double ms);
    long long request_count() const { return request_count_.load(); }
    long long response_count() const { return response_count_.load(); }
    int active_connections() const { return active_connections_.load(); }
    double avg_response_time_ms() const;
    std::time_t start_time() const { return start_time_; }

private:
    void onConnection(const muduo::net::TcpConnectionPtr& conn);
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp receiveTime);
    void handleRequest(const muduo::net::TcpConnectionPtr& conn,
                       const std::string& raw_request);
    bool shouldClose(const HttpRequest& req) const;

    muduo::net::EventLoop loop_;
    muduo::net::TcpServer server_;
    Router router_;
    MiddlewareChain middlewares_;
    std::shared_ptr<SessionManager> session_manager_;
    size_t max_body_size_{1024 * 1024};
    int thread_num_{1};

    // Stats tracking
    std::atomic<long long> request_count_{0};
    std::atomic<long long> response_count_{0};
    std::atomic<int> active_connections_{0};
    std::atomic<double> total_response_time_ms_{0};
    std::time_t start_time_{std::time(nullptr)};
};

} // namespace muduo_http

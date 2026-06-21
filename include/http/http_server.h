#pragma once

#include <cstddef>
#include <memory>

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
};

} // namespace muduo_http

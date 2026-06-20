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
    size_t max_body_size_{1024 * 1024};  // 1MB default
};

} // namespace muduo_http

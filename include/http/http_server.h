#pragma once

#include <memory>

#include "muduo/net/TcpServer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/TcpConnection.h"

#include "http/middleware.h"
#include "http/router.h"

namespace muduo_http {

class HttpServer {
public:
    explicit HttpServer(int port);

    void Start();

    Router& routes();
    void Use(Middleware middleware);

private:
    void onConnection(const muduo::net::TcpConnectionPtr& conn);
    void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
                   muduo::Timestamp receiveTime);

    muduo::net::EventLoop loop_;
    muduo::net::TcpServer server_;
    Router router_;
    MiddlewareChain middlewares_;
};

} // namespace muduo_http

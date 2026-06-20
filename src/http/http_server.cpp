#include "http/http_server.h"

#include <iostream>

#include "http/http_context.h"
#include "http/http_response.h"

namespace muduo_http {

HttpServer::HttpServer(int port)
    : server_(&loop_,
              muduo::net::InetAddress(static_cast<uint16_t>(port)),
              "muduo_http"),
      router_(),
      middlewares_() {
    server_.setConnectionCallback(
        std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&HttpServer::onMessage, this,
                  std::placeholders::_1,
                  std::placeholders::_2,
                  std::placeholders::_3));
}

void HttpServer::Start() {
    std::cout << "HTTP server starting on 0.0.0.0:"
              << server_.ipPort() << '\n';
    server_.start();
    loop_.loop();
}

void HttpServer::onConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
        std::cout << "[http] new connection: "
                  << conn->peerAddress().toIpPort() << '\n';
    } else {
        std::cout << "[http] connection closed: "
                  << conn->peerAddress().toIpPort() << '\n';
    }
}

void HttpServer::onMessage(const muduo::net::TcpConnectionPtr& conn,
                           muduo::net::Buffer* buf,
                           muduo::Timestamp) {
    const std::string raw_request(buf->retrieveAllAsString());

    HttpContext context;
    HttpResponse response;

    if (!context.ParseRequest(raw_request)) {
        response.SetStatusCode(400);
        response.SetStatusMessage("Bad Request");
        response.SetBody("400 Bad Request\n");
    } else if (middlewares_.Run(context.request(), response)) {
        router_.Route(context.request(), response);
    }

    conn->send(response.ToString());
    conn->shutdown();
}

Router& HttpServer::routes() {
    return router_;
}

void HttpServer::Use(Middleware middleware) {
    middlewares_.Use(std::move(middleware));
}

} // namespace muduo_http


#include "http/http_server.h"

#include <iostream>
#include <string>

#include "http/http_context.h"
#include "http/http_response.h"

namespace muduo_http {

namespace {

void SetErrorResponse(HttpResponse& response, int code, const std::string& message) {
    response.SetStatusCode(code);
    response.SetStatusMessage(message);
    response.SetHeader("Content-Type", "text/plain; charset=utf-8");
    response.SetBody(std::to_string(code) + " " + message + "\n");
}

} // anonymous namespace

HttpServer::HttpServer(int port)
    : server_(&loop_,
              muduo::net::InetAddress(static_cast<uint16_t>(port)),
              "muduo_http"),
      router_(),
      middlewares_(),
      session_manager_(std::make_shared<SessionManager>()) {
    server_.setConnectionCallback(
        std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&HttpServer::onMessage, this,
                  std::placeholders::_1,
                  std::placeholders::_2,
                  std::placeholders::_3));

    // Register default session middleware
    middlewares_.Use(CreateSessionMiddleware(session_manager_));
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
    }
}

void HttpServer::onMessage(const muduo::net::TcpConnectionPtr& conn,
                           muduo::net::Buffer* buf,
                           muduo::Timestamp) {
    const std::string raw_request(buf->retrieveAllAsString());
    handleRequest(conn, raw_request);
}

void HttpServer::handleRequest(const muduo::net::TcpConnectionPtr& conn,
                               const std::string& raw_request) {
    HttpContext context;
    HttpResponse response;

    if (!context.ParseRequest(raw_request)) {
        // Map ParseResult to HTTP status code
        switch (context.result()) {
            case ParseResult::kMethodNotAllowed:
                SetErrorResponse(response, 405, "Method Not Allowed");
                response.SetHeader("Allow", router_.AllowedMethods(context.request().path));
                break;
            case ParseResult::kEntityTooLarge:
                SetErrorResponse(response, 413, "Payload Too Large");
                break;
            case ParseResult::kVersionNotSupported:
                SetErrorResponse(response, 505, "HTTP Version Not Supported");
                break;
            default:
                SetErrorResponse(response, 400, "Bad Request");
                break;
        }
    } else {
        const auto& req = context.request();

        try {
            if (middlewares_.Run(req, response)) {
                router_.Route(req, response);
            }
        } catch (const std::exception& e) {
            SetErrorResponse(response, 500, "Internal Server Error");
            std::cerr << "[http] unhandled exception: " << e.what() << '\n';
        } catch (...) {
            SetErrorResponse(response, 500, "Internal Server Error");
            std::cerr << "[http] unknown unhandled exception\n";
        }
    }

    // --- Connection management ---
    bool close = shouldClose(context.request());
    response.SetHeader("Connection", close ? "close" : "keep-alive");

    // Server header
    response.SetHeader("Server", "muduo_http/0.1");

    conn->send(response.ToString());
    if (close) {
        conn->shutdown();
    }
}

bool HttpServer::shouldClose(const HttpRequest& req) const {
    auto it = req.headers.find("Connection");
    if (it != req.headers.end()) {
        // Case-insensitive compare
        const auto& val = it->second;
        if (val.size() == 5 &&
            (val[0] == 'c' || val[0] == 'C') &&
            (val[1] == 'l' || val[1] == 'L') &&
            (val[2] == 'o' || val[2] == 'O') &&
            (val[3] == 's' || val[3] == 'S') &&
            (val[4] == 'e' || val[4] == 'E')) {
            return true;
        }
    }
    // HTTP/1.0 defaults to close
    if (req.version == "HTTP/1.0") {
        return true;
    }
    return false;
}

Router& HttpServer::routes() {
    return router_;
}

void HttpServer::Use(Middleware middleware) {
    middlewares_.Use(std::move(middleware));
}

void HttpServer::set_session_timeout(int seconds) {
    session_manager_ = std::make_shared<SessionManager>(seconds);
}

} // namespace muduo_http


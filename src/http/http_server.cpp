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
              << server_.ipPort() << " (threads=" << thread_num_ << ")\n";
    server_.setThreadNum(thread_num_);
    server_.start();
    loop_.loop();
}

void HttpServer::onConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
        active_connections_++;
        std::cout << "[http] new connection: "
                  << conn->peerAddress().toIpPort() << '\n';
    } else {
        active_connections_--;
    }
}

void HttpServer::onMessage(const muduo::net::TcpConnectionPtr& conn,
                           muduo::net::Buffer* buf,
                           muduo::Timestamp) {
    std::string new_data(buf->retrieveAllAsString());

    // Append to partial buffer if previous request was incomplete
    auto& acc = partial_requests_[conn.get()];
    acc += new_data;

    request_count_++;
    handleRequest(conn, acc);

    // If request was fully processed, clear the buffer
    // (handler sets stream flag for async, ParseResult::kIncomplete for partial)
}

void HttpServer::handleRequest(const muduo::net::TcpConnectionPtr& conn,
                               const std::string& raw_request) {
    HttpContext context;
    HttpResponse response;

    if (!context.ParseRequest(raw_request)) {
        // Check if request is incomplete — wait for more data
        if (context.result() == ParseResult::kIncomplete) {
            return;  // Keep data in partial_requests_, wait for next onMessage
        }
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
        auto& req = context.request();

        // Expose connection for streaming support
        req.stream_conn = conn;

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

    // --- If handler activated streaming mode, skip normal response ---
    if (context.request().stream) {
        // Clear partial buffer for this connection (streaming handled elsewhere)
        partial_requests_.erase(conn.get());
        return;
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

    // Clear partial buffer after successful response
    partial_requests_.erase(conn.get());
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

void HttpServer::TrackResponseTime(double ms) {
    // Approximate: running average
    double current = total_response_time_ms_.load();
    double count = static_cast<double>(response_count_.load() + 1);
    total_response_time_ms_.store(current + ms);
}

double HttpServer::avg_response_time_ms() const {
    long long count = response_count_.load();
    if (count == 0) return 0.0;
    return total_response_time_ms_.load() / static_cast<double>(count);
}

} // namespace muduo_http


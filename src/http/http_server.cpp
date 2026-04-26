#include "http/http_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <utility>
#include <unistd.h>

#include "http/http_context.h"
#include "http/http_response.h"

namespace muduo_http {

HttpServer::HttpServer(int port)
    : port_(port) {}

void HttpServer::Start() {
    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << '\n';
        return;
    }

    int reuse = 1;
    if (::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno) << '\n';
        ::close(listen_fd);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed on port " << port_ << ": " << std::strerror(errno) << '\n';
        ::close(listen_fd);
        return;
    }

    if (::listen(listen_fd, SOMAXCONN) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << '\n';
        ::close(listen_fd);
        return;
    }

    std::cout << "HTTP server listening on 0.0.0.0:" << port_ << '\n';

    for (;;) {
        const int conn_fd = ::accept(listen_fd, nullptr, nullptr);
        if (conn_fd < 0) {
            std::cerr << "accept() failed: " << std::strerror(errno) << '\n';
            continue;
        }

        char buffer[8192];
        const ssize_t n = ::recv(conn_fd, buffer, sizeof(buffer), 0);

        HttpResponse response;
        if (n <= 0) {
            response.SetStatusCode(400);
            response.SetStatusMessage("Bad Request");
            response.SetBody("400 Bad Request\n");
        } else {
            HttpContext context;
            const std::string raw_request(buffer, static_cast<size_t>(n));
            if (!context.ParseRequest(raw_request)) {
                response.SetStatusCode(400);
                response.SetStatusMessage("Bad Request");
                response.SetBody("400 Bad Request\n");
            } else if (middlewares_.Run(context.request(), response)) {
                router_.Route(context.request(), response);
            }
        }

        const std::string raw_response = response.ToString();
        const char* data = raw_response.data();
        size_t left = raw_response.size();
        while (left > 0) {
            const ssize_t sent = ::send(conn_fd, data, left, 0);
            if (sent <= 0) {
                break;
            }
            data += sent;
            left -= static_cast<size_t>(sent);
        }

        ::close(conn_fd);
    }
}

Router& HttpServer::routes() {
    return router_;
}

void HttpServer::Use(Middleware middleware) {
    middlewares_.Use(std::move(middleware));
}

} // namespace muduo_http


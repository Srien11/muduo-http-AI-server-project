#include "http/https_server.h"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <unistd.h>

#include "http/http_context.h"
#include "http/http_response.h"

namespace muduo_http {

HttpsServer::HttpsServer(int port, std::string cert_file, std::string key_file)
    : port_(port),
      cert_file_(std::move(cert_file)),
      key_file_(std::move(key_file)) {}

HttpsServer::~HttpsServer() {
    Stop();
}

void HttpsServer::Start() {
    SslConfig config;
    config.cert_file = cert_file_;
    config.key_file = key_file_;

    if (!ssl_ctx_.LoadCertificate(config)) {
        std::cerr << "[https] SSL init failed, not starting\n";
        return;
    }

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[https] socket() failed: " << std::strerror(errno) << '\n';
        return;
    }

    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[https] bind() failed on port " << port_
                  << ": " << std::strerror(errno) << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::listen(listen_fd_, SOMAXCONN) < 0) {
        std::cerr << "[https] listen() failed: " << std::strerror(errno) << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    std::cout << "[https] server listening on 0.0.0.0:" << port_ << '\n';

    running_ = true;
    thread_ = std::thread(&HttpsServer::Serve, this);
}

void HttpsServer::Serve() {
    char buf[8192];

    while (running_) {
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        const int conn_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (conn_fd < 0) {
            if (running_) {
                std::cerr << "[https] accept() failed: " << std::strerror(errno) << '\n';
            }
            continue;
        }

        SSL* ssl = ssl_ctx_.CreateSsl(conn_fd);
        if (!ssl) {
            ::close(conn_fd);
            continue;
        }

        int ret = SSL_accept(ssl);
        if (ret <= 0) {
            std::cerr << "[https] SSL_accept failed\n";
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            ::close(conn_fd);
            continue;
        }

        const ssize_t n = SSL_read(ssl, buf, sizeof(buf));
        if (n <= 0) {
            SSL_free(ssl);
            ::close(conn_fd);
            continue;
        }

        HttpContext context;
        HttpResponse response;
        const std::string raw_request(buf, static_cast<size_t>(n));

        char peer_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
        std::cout << "[https] request from " << peer_ip << '\n';

        if (!context.ParseRequest(raw_request)) {
            response.SetStatusCode(400);
            response.SetStatusMessage("Bad Request");
            response.SetBody("400 Bad Request\n");
        } else if (middlewares_.Run(context.request(), response)) {
            router_.Route(context.request(), response);
        }

        response.SetHeader("Connection", "close");
        const std::string raw_response = response.ToString();
        SSL_write(ssl, raw_response.data(), static_cast<int>(raw_response.size()));

        SSL_shutdown(ssl);
        SSL_free(ssl);
        ::close(conn_fd);
    }
}

void HttpsServer::Stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

} // namespace muduo_http

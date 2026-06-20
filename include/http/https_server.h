#pragma once

#include <memory>
#include <string>
#include <thread>

#include "http/http_context.h"
#include "http/http_response.h"
#include "http/middleware.h"
#include "http/router.h"
#include "http/ssl_context.h"

namespace muduo_http {

class HttpsServer {
public:
    HttpsServer(int port, std::string cert_file, std::string key_file);
    ~HttpsServer();

    void Start();
    void Stop();

    Router& routes() { return router_; }
    void Use(Middleware middleware) { middlewares_.Use(std::move(middleware)); }

private:
    void Serve();

    int port_;
    std::string cert_file_;
    std::string key_file_;
    int listen_fd_{-1};
    bool running_{false};
    std::thread thread_;

    SslContext ssl_ctx_;
    Router router_;
    MiddlewareChain middlewares_;
};

} // namespace muduo_http

#pragma once

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
    int port_{0};
    Router router_;
    MiddlewareChain middlewares_;
};

} // namespace muduo_http

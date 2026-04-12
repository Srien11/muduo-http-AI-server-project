#pragma once

#include "http/router.h"

namespace muduo_http {

class HttpServer {
public:
    explicit HttpServer(int port);
    void Start();

    Router& routes();

private:
    int port_{0};
    Router router_;
};

} // namespace muduo_http

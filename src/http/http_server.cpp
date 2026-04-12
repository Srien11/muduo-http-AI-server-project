#include "http/http_server.h"

namespace muduo_http {

HttpServer::HttpServer(int port)
    : port_(port) {}

void HttpServer::Start() {
}

Router& HttpServer::routes() {
    return router_;
}

} // namespace muduo_http

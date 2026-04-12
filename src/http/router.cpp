#include "http/router.h"

namespace muduo_http {

void Router::Get(const std::string& path, RouteHandler handler) {
    get_routes_[path] = std::move(handler);
}

void Router::Post(const std::string& path, RouteHandler handler) {
    post_routes_[path] = std::move(handler);
}

} // namespace muduo_http

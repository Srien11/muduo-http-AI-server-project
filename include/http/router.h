#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "http/http_request.h"
#include "http/http_response.h"

namespace muduo_http {

using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class Router {
public:
    void Get(const std::string& path, RouteHandler handler);
    void Post(const std::string& path, RouteHandler handler);

private:
    std::unordered_map<std::string, RouteHandler> get_routes_;
    std::unordered_map<std::string, RouteHandler> post_routes_;
};

} // namespace muduo_http

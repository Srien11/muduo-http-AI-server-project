#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "http/http_request.h"
#include "http/http_response.h"

namespace muduo_http {

using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class Router {
public:
    void Get(const std::string& path, RouteHandler handler);
    void Post(const std::string& path, RouteHandler handler);

    bool Route(const HttpRequest& request, HttpResponse& response) const;

private:
    struct RouteEntry {
        std::string pattern;
        RouteHandler handler;
    };

    std::unordered_map<std::string, RouteHandler> get_routes_;
    std::unordered_map<std::string, RouteHandler> post_routes_;
    std::vector<RouteEntry> dynamic_get_routes_;
    std::vector<RouteEntry> dynamic_post_routes_;
};

} // namespace muduo_http

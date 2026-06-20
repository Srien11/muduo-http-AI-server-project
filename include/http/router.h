#pragma once

#include <functional>
#include <set>
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
    void Put(const std::string& path, RouteHandler handler);
    void Delete(const std::string& path, RouteHandler handler);

    bool Route(const HttpRequest& request, HttpResponse& response) const;
    std::string AllowedMethods(const std::string& path) const;

private:
    struct RouteEntry {
        std::string pattern;
        RouteHandler handler;
    };

    using RouteMap = std::unordered_map<std::string, RouteHandler>;
    using DynamicRoutes = std::vector<RouteEntry>;

    void RegisterRoute(const std::string& method,
                       const std::string& path,
                       RouteHandler handler,
                       RouteMap& static_map,
                       DynamicRoutes& dynamic_map);

    bool MatchStatic(const std::string& method,
                     const std::string& path,
                     const HttpRequest& request,
                     HttpResponse& response) const;
    bool MatchDynamic(const std::string& method,
                      const std::string& path,
                      const HttpRequest& request,
                      HttpResponse& response) const;

    RouteMap get_routes_;
    RouteMap post_routes_;
    RouteMap put_routes_;
    RouteMap delete_routes_;
    DynamicRoutes dynamic_get_routes_;
    DynamicRoutes dynamic_post_routes_;
    DynamicRoutes dynamic_put_routes_;
    DynamicRoutes dynamic_delete_routes_;

    // Tracks which paths have registered routes (for 405)
    // Each entry maps path -> set of HTTP methods
    mutable std::unordered_map<std::string, std::set<std::string>> path_methods_;
};

} // namespace muduo_http

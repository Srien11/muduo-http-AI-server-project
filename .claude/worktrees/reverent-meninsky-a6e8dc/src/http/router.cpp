#include "http/router.h"

#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>

namespace muduo_http {

namespace {

bool IsDynamicPattern(const std::string& pattern) {
    return pattern.find(':') != std::string::npos;
}

std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::istringstream stream(path);
    std::string part;

    while (std::getline(stream, part, '/')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    return parts;
}

bool MatchPath(const std::string& pattern,
               const std::string& path,
               std::unordered_map<std::string, std::string>& params) {
    const auto pattern_parts = SplitPath(pattern);
    const auto path_parts = SplitPath(path);

    if (pattern_parts.size() != path_parts.size()) {
        return false;
    }

    std::unordered_map<std::string, std::string> matched_params;
    for (std::size_t i = 0; i < pattern_parts.size(); ++i) {
        const auto& pattern_part = pattern_parts[i];
        const auto& path_part = path_parts[i];

        if (!pattern_part.empty() && pattern_part.front() == ':') {
            matched_params[pattern_part.substr(1)] = path_part;
            continue;
        }

        if (pattern_part != path_part) {
            return false;
        }
    }

    params = std::move(matched_params);
    return true;
}

// Track the path in the path_methods_ map for 405 support
void TrackPath(std::unordered_map<std::string, std::set<std::string>>& path_methods,
               const std::string& path,
               const std::string& method) {
    // Don't track dynamic patterns individually
    if (!IsDynamicPattern(path)) {
        path_methods[path].insert(method);
    }
}

} // namespace

void Router::RegisterRoute(const std::string& method,
                           const std::string& path,
                           RouteHandler handler,
                           RouteMap& static_map,
                           DynamicRoutes& dynamic_map) {
    if (IsDynamicPattern(path)) {
        dynamic_map.push_back(RouteEntry{path, std::move(handler)});
    } else {
        static_map[path] = std::move(handler);
    }
    TrackPath(path_methods_, path, method);
}

void Router::Get(const std::string& path, RouteHandler handler) {
    RegisterRoute("GET", path, std::move(handler), get_routes_, dynamic_get_routes_);
}

void Router::Post(const std::string& path, RouteHandler handler) {
    RegisterRoute("POST", path, std::move(handler), post_routes_, dynamic_post_routes_);
}

void Router::Put(const std::string& path, RouteHandler handler) {
    RegisterRoute("PUT", path, std::move(handler), put_routes_, dynamic_put_routes_);
}

void Router::Delete(const std::string& path, RouteHandler handler) {
    RegisterRoute("DELETE", path, std::move(handler), delete_routes_, dynamic_delete_routes_);
}

bool Router::MatchStatic(const std::string& method,
                         const std::string& path,
                         const HttpRequest& request,
                         HttpResponse& response) const {
    const RouteMap* map = nullptr;
    if (method == "GET")         map = &get_routes_;
    else if (method == "POST")   map = &post_routes_;
    else if (method == "PUT")    map = &put_routes_;
    else if (method == "DELETE") map = &delete_routes_;
    else                         return false;

    const auto it = map->find(path);
    if (it != map->end()) {
        it->second(request, response);
        return true;
    }
    return false;
}

bool Router::MatchDynamic(const std::string& method,
                          const std::string& path,
                          const HttpRequest& request,
                          HttpResponse& response) const {
    const DynamicRoutes* dyn_map = nullptr;
    if (method == "GET")         dyn_map = &dynamic_get_routes_;
    else if (method == "POST")   dyn_map = &dynamic_post_routes_;
    else if (method == "PUT")    dyn_map = &dynamic_put_routes_;
    else if (method == "DELETE") dyn_map = &dynamic_delete_routes_;
    else                         return false;

    for (const auto& route : *dyn_map) {
        HttpRequest matched_request = request;
        if (MatchPath(route.pattern, path, matched_request.path_params)) {
            route.handler(matched_request, response);
            return true;
        }
    }
    return false;
}

bool Router::Route(const HttpRequest& request, HttpResponse& response) const {
    // Try static routes first
    if (MatchStatic(request.method, request.path, request, response)) {
        return true;
    }

    // Try dynamic routes
    if (MatchDynamic(request.method, request.path, request, response)) {
        return true;
    }

    // Check if the path exists with a different method → 405
    const auto methods_it = path_methods_.find(request.path);
    if (methods_it != path_methods_.end()) {
        response.SetStatusCode(405);
        response.SetStatusMessage("Method Not Allowed");
        response.SetHeader("Allow", AllowedMethods(request.path));
        response.SetBody("405 Method Not Allowed\n");
        return false;
    }

    // Path doesn't exist at all → 404
    response.SetStatusCode(404);
    response.SetStatusMessage("Not Found");
    response.SetBody("404 Not Found\n");
    return false;
}

std::string Router::AllowedMethods(const std::string& path) const {
    const auto it = path_methods_.find(path);
    if (it == path_methods_.end()) {
        return "";
    }

    std::ostringstream oss;
    bool first = true;
    for (const auto& method : it->second) {
        if (!first) {
            oss << ", ";
        }
        oss << method;
        first = false;
    }
    return oss.str();
}

} // namespace muduo_http

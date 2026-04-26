#include "http/router.h"

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

} // namespace

void Router::Get(const std::string& path, RouteHandler handler) {
    if (IsDynamicPattern(path)) {
        dynamic_get_routes_.push_back(RouteEntry{path, std::move(handler)});
        return;
    }

    get_routes_[path] = std::move(handler);
}

void Router::Post(const std::string& path, RouteHandler handler) {
    if (IsDynamicPattern(path)) {
        dynamic_post_routes_.push_back(RouteEntry{path, std::move(handler)});
        return;
    }

    post_routes_[path] = std::move(handler);
}

bool Router::Route(const HttpRequest& request, HttpResponse& response) const {
    const auto* routes = request.method == "GET" ? &get_routes_ : &post_routes_;
    const auto* dynamic_routes = request.method == "GET" ? &dynamic_get_routes_ : &dynamic_post_routes_;

    const auto it = routes->find(request.path);
    if (it != routes->end()) {
        it->second(request, response);
        return true;
    }

    for (const auto& route : *dynamic_routes) {
        HttpRequest matched_request = request;
        if (MatchPath(route.pattern, request.path, matched_request.path_params)) {
            route.handler(matched_request, response);
            return true;
        }
    }

    response.SetStatusCode(404);
    response.SetStatusMessage("Not Found");
    response.SetBody("404 Not Found\n");
    return false;
}

} // namespace muduo_http

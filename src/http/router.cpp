#include "http/router.h"

#include <utility>

namespace muduo_http {

void Router::Get(const std::string& path, RouteHandler handler) {
    get_routes_[path] = std::move(handler);
}

void Router::Post(const std::string& path, RouteHandler handler) {
    post_routes_[path] = std::move(handler);
}

bool Router::Route(const HttpRequest& request, HttpResponse& response) const {
    const auto* routes = request.method == "GET" ? &get_routes_ : &post_routes_;

    const auto it = routes->find(request.path);
    if (it != routes->end()) {
        it->second(request, response);
        return true;
    }

    response.SetStatusCode(404);
    response.SetStatusMessage("Not Found");
    response.SetBody("404 Not Found\n");
    return false;
}

} // namespace muduo_http

#include "http/middleware.h"

#include <iostream>
#include <utility>

namespace muduo_http {

void MiddlewareChain::Use(Middleware middleware) {
    middlewares_.push_back(std::move(middleware));
}

bool MiddlewareChain::Run(const HttpRequest& request, HttpResponse& response) const {
    for (const auto& middleware : middlewares_) {
        if (!middleware(request, response)) {
            return false;
        }
    }

    return true;
}

Middleware CreateLoggingMiddleware() {
    return [](const HttpRequest& request, HttpResponse&) {
        std::cout << "[http] " << request.method << ' ' << request.path << std::endl;
        return true;
    };
}

Middleware CreateCorsMiddleware() {
    return [](const HttpRequest& request, HttpResponse& response) {
        response.SetHeader("Access-Control-Allow-Origin", "*");
        response.SetHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        response.SetHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

        if (request.method == "OPTIONS") {
            response.SetStatusCode(204);
            response.SetStatusMessage("No Content");
            return false;
        }

        return true;
    };
}

} // namespace muduo_http

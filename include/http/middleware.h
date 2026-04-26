#pragma once

#include <functional>
#include <vector>

#include "http/http_request.h"
#include "http/http_response.h"

namespace muduo_http {

using Middleware = std::function<bool(const HttpRequest&, HttpResponse&)>;

class MiddlewareChain {
public:
    void Use(Middleware middleware);
    bool Run(const HttpRequest& request, HttpResponse& response) const;

private:
    std::vector<Middleware> middlewares_;
};

Middleware CreateLoggingMiddleware();
Middleware CreateCorsMiddleware();

} // namespace muduo_http

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "http/http_request.h"
#include "http/http_response.h"

namespace muduo_http {

class SessionManager;

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
Middleware CreateSessionMiddleware(std::shared_ptr<SessionManager> session_manager);

// Helper: extract cookie value by name from Cookie header
std::string GetCookieValue(const HttpRequest& request, const std::string& name);

} // namespace muduo_http

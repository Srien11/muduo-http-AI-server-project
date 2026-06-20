#include "http/middleware.h"
#include "http/session.h"

#include <algorithm>
#include <iostream>
#include <sstream>
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
        response.SetHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        response.SetHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");

        if (request.method == "OPTIONS") {
            response.SetStatusCode(204);
            response.SetStatusMessage("No Content");
            return false;
        }

        return true;
    };
}

Middleware CreateSessionMiddleware(std::shared_ptr<SessionManager> session_manager) {
    return [session_manager](const HttpRequest& request, HttpResponse& response) {
        if (!session_manager) {
            return true;
        }

        // Extract session ID from cookie
        const std::string session_id = GetCookieValue(request, "SESSION_ID");
        std::shared_ptr<Session> session;

        if (!session_id.empty()) {
            session = session_manager->Get(session_id);
        }

        // Create new session if none exists
        if (!session) {
            session = session_manager->Create();
            response.SetHeader("Set-Cookie",
                               "SESSION_ID=" + session->id() +
                               "; Path=/; HttpOnly; SameSite=Lax");
        }

        request.session = session;
        return true;
    };
}

std::string GetCookieValue(const HttpRequest& request, const std::string& name) {
    auto it = request.headers.find("Cookie");
    if (it == request.headers.end()) {
        return "";
    }

    const std::string& cookie_header = it->second;
    const std::string target = name + "=";

    size_t pos = 0;
    while (pos < cookie_header.size()) {
        // Skip spaces and semicolons
        while (pos < cookie_header.size() &&
               (cookie_header[pos] == ' ' || cookie_header[pos] == ';')) {
            ++pos;
        }

        // Check if this cookie matches
        if (cookie_header.compare(pos, target.size(), target) == 0) {
            pos += target.size();
            const size_t end = cookie_header.find(';', pos);
            return cookie_header.substr(pos, end - pos);
        }

        // Skip to next semicolon
        const size_t next = cookie_header.find(';', pos);
        if (next == std::string::npos) break;
        pos = next + 1;
    }

    return "";
}

} // namespace muduo_http

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace muduo_http {

class Session;

class HttpRequest {
public:
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> path_params;
    std::string body;

    // Attached by session middleware during request processing
    mutable std::shared_ptr<Session> session;
};

} // namespace muduo_http

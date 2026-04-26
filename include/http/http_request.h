#pragma once

#include <string>
#include <unordered_map>

namespace muduo_http {

class HttpRequest {
public:
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> path_params;
    std::string body;
};

} // namespace muduo_http

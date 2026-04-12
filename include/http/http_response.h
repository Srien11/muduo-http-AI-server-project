#pragma once

#include <string>
#include <unordered_map>

namespace muduo_http {

class HttpResponse {
public:
    int status_code{200};
    std::string status_message{"OK"};
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

} // namespace muduo_http

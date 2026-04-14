#pragma once

#include <string>

#include "http/http_request.h"

namespace muduo_http {

class HttpContext {
public:
    bool ParseRequest(const std::string& raw_request);

    const HttpRequest& request() const;

private:
    HttpRequest request_;
};

} // namespace muduo_http

#pragma once

#include "http/http_request.h"

namespace muduo_http {

class HttpContext {
public:
    HttpRequest request;
};

} // namespace muduo_http

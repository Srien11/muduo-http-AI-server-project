#pragma once

#include <string>

#include "http/http_request.h"

namespace muduo_http {

enum class ParseResult {
    kOk,
    kBadRequest,
    kMethodNotAllowed,
    kEntityTooLarge,
    kVersionNotSupported,
    kHeaderTooLarge,
};

class HttpContext {
public:
    bool ParseRequest(const std::string& raw_request);

    ParseResult result() const { return result_; }
    const HttpRequest& request() const { return request_; }

private:
    static bool IsValidMethod(const std::string& method);
    static bool IsValidVersion(const std::string& version);

    HttpRequest request_;
    ParseResult result_{ParseResult::kOk};
};

} // namespace muduo_http

#include "http/http_response.h"

#include <sstream>
#include <utility>

namespace muduo_http {

void HttpResponse::SetStatusCode(int status_code) {
    status_code_ = status_code;
}

void HttpResponse::SetStatusMessage(std::string status_message) {
    status_message_ = std::move(status_message);
}

void HttpResponse::SetHeader(std::string key, std::string value) {
    headers_[std::move(key)] = std::move(value);
}

void HttpResponse::SetBody(std::string body) {
    body_ = std::move(body);
}

int HttpResponse::status_code() const {
    return status_code_;
}

const std::string& HttpResponse::status_message() const {
    return status_message_;
}

const std::unordered_map<std::string, std::string>& HttpResponse::headers() const {
    return headers_;
}

const std::string& HttpResponse::body() const {
    return body_;
}

std::string HttpResponse::ToString() const {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code_ << ' ' << status_message_ << "\r\n";

    auto headers = headers_;
    if (headers.find("Content-Type") == headers.end()) {
        headers["Content-Type"] = "text/plain; charset=utf-8";
    }
    headers["Content-Length"] = std::to_string(body_.size());

    for (const auto& [key, value] : headers) {
        response << key << ": " << value << "\r\n";
    }

    response << "\r\n";
    response << body_;
    return response.str();
}

} // namespace muduo_http

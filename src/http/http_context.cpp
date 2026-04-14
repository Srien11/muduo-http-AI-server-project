#include "http/http_context.h"

#include <sstream>

namespace muduo_http {

bool HttpContext::ParseRequest(const std::string& raw_request) {
    request_ = HttpRequest{};

    std::istringstream stream(raw_request);
    std::string line;

    if (!std::getline(stream, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    {
        std::istringstream request_line(line);
        if (!(request_line >> request_.method >> request_.path >> request_.version)) {
            return false;
        }
    }

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }

        const auto pos = line.find(':');
        if (pos == std::string::npos) {
            return false;
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(0, 1);
        }

        request_.headers[std::move(key)] = std::move(value);
    }

    std::string body;
    std::string body_line;
    bool first = true;
    while (std::getline(stream, body_line)) {
        if (!body_line.empty() && body_line.back() == '\r') {
            body_line.pop_back();
        }
        if (!first) {
            body += "\n";
        }
        body += body_line;
        first = false;
    }
    request_.body = std::move(body);

    return true;
}

const HttpRequest& HttpContext::request() const {
    return request_;
}

} // namespace muduo_http

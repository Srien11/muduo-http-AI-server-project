#include "http/http_context.h"

#include <cctype>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace muduo_http {

namespace {

constexpr size_t kMaxHeaderSize = 8192;       // 8KB
constexpr size_t kMaxBodySize = 1024 * 1024;  // 1MB
constexpr size_t kMaxRequestLineSize = 4096;  // 4KB

const std::unordered_set<std::string>& KnownMethods() {
    static const std::unordered_set<std::string> methods = {
        "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS", "TRACE", "CONNECT"
    };
    return methods;
}

const std::unordered_set<std::string>& KnownVersions() {
    static const std::unordered_set<std::string> versions = {
        "HTTP/1.0", "HTTP/1.1"
    };
    return versions;
}

bool TryParseContentLength(const std::string& value, size_t& out) {
    char* end = nullptr;
    const unsigned long long val = std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        return false;  // not a valid integer
    }
    if (val > std::numeric_limits<size_t>::max()) {
        return false;
    }
    out = static_cast<size_t>(val);
    return true;
}

} // anonymous namespace

bool HttpContext::IsValidMethod(const std::string& method) {
    return KnownMethods().find(method) != KnownMethods().end();
}

bool HttpContext::IsValidVersion(const std::string& version) {
    return KnownVersions().find(version) != KnownVersions().end();
}

bool HttpContext::ParseRequest(const std::string& raw_request) {
    request_ = HttpRequest{};
    result_ = ParseResult::kOk;

    if (raw_request.empty()) {
        result_ = ParseResult::kBadRequest;
        return false;
    }

    // Check total request size
    if (raw_request.size() > kMaxHeaderSize + kMaxBodySize) {
        result_ = ParseResult::kEntityTooLarge;
        return false;
    }

    std::istringstream stream(raw_request);
    std::string line;

    // -------- Request Line --------
    if (!std::getline(stream, line)) {
        result_ = ParseResult::kBadRequest;
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    if (line.size() > kMaxRequestLineSize) {
        result_ = ParseResult::kBadRequest;
        return false;
    }

    {
        std::istringstream request_line(line);
        if (!(request_line >> request_.method >> request_.path >> request_.version)) {
            result_ = ParseResult::kBadRequest;
            return false;
        }

        // Validate HTTP method
        if (!IsValidMethod(request_.method)) {
            result_ = ParseResult::kMethodNotAllowed;
            return false;
        }

        // Validate HTTP version
        if (!IsValidVersion(request_.version)) {
            result_ = ParseResult::kVersionNotSupported;
            return false;
        }
    }

    // -------- Headers --------
    size_t header_bytes = 0;
    size_t content_length = 0;
    bool has_content_length = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        header_bytes += line.size() + 2;  // +2 for \r\n
        if (header_bytes > kMaxHeaderSize) {
            result_ = ParseResult::kHeaderTooLarge;
            return false;
        }

        // Empty line marks end of headers
        if (line.empty()) {
            break;
        }

        const auto pos = line.find(':');
        if (pos == std::string::npos) {
            result_ = ParseResult::kBadRequest;
            return false;
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(0, 1);
        }

        // Content-Length: validate and parse
        if (std::equal(key.begin(), key.end(),
                       "Content-Length",
                       [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
            if (has_content_length) {
                // Duplicate Content-Length is an error
                result_ = ParseResult::kBadRequest;
                return false;
            }
            if (!TryParseContentLength(value, content_length)) {
                result_ = ParseResult::kBadRequest;
                return false;
            }
            if (content_length > kMaxBodySize) {
                result_ = ParseResult::kEntityTooLarge;
                return false;
            }
            has_content_length = true;
        }

        request_.headers[std::move(key)] = std::move(value);
    }

    // -------- Body --------
    if (has_content_length && content_length > 0) {
        const auto body_start = raw_request.find("\r\n\r\n");
        if (body_start == std::string::npos) {
            result_ = ParseResult::kBadRequest;
            return false;
        }

        const size_t body_offset = body_start + 4;
        const size_t bytes_available = raw_request.size() - body_offset;

        if (bytes_available < content_length) {
            result_ = ParseResult::kBadRequest;
            return false;
        }

        request_.body = raw_request.substr(body_offset, content_length);
    }

    return true;
}

} // namespace muduo_http

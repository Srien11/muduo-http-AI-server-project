#pragma once

#include <string>
#include <unordered_map>

namespace muduo_http {

class HttpResponse {
public:
    void SetStatusCode(int status_code);
    void SetStatusMessage(std::string status_message);
    void SetHeader(std::string key, std::string value);
    void SetBody(std::string body);

    int status_code() const;
    const std::string& status_message() const;
    const std::unordered_map<std::string, std::string>& headers() const;
    const std::string& body() const;

    std::string ToString() const;

private:
    int status_code_{200};
    std::string status_message_{"OK"};
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

} // namespace muduo_http

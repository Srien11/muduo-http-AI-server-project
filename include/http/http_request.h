#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "muduo/net/TcpConnection.h"

namespace muduo_http {

class Session;
class StreamWriter;

class HttpRequest {
public:
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> path_params;
    std::string body;

    // Attached by middleware/server during request processing
    mutable std::shared_ptr<Session> session;

    // Set by server for streaming support; handler uses this to create StreamWriter
    mutable muduo::net::TcpConnectionPtr stream_conn;

    // Set by handler to switch to streaming mode (SSE/chunked)
    mutable std::shared_ptr<StreamWriter> stream;
};

} // namespace muduo_http

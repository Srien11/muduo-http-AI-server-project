#pragma once

#include <memory>
#include <string>

#include "muduo/net/TcpConnection.h"

namespace muduo_http {

class StreamWriter {
public:
    StreamWriter(const muduo::net::TcpConnectionPtr& conn,
                 int status_code = 200,
                 const std::string& status_message = "OK",
                 const std::string& content_type = "text/event-stream");

    // Send a chunk of data (chunked transfer encoding)
    void WriteChunk(const std::string& data);

    // Send SSE event (formats as "data: ...\n\n")
    void WriteSSE(const std::string& data);
    void WriteSSE(const std::string& event, const std::string& data);
    void WriteSSE(const std::string& event, const std::string& data, const std::string& id);

    // Send raw data directly
    void WriteRaw(const std::string& data);

    // End the stream (sends final zero-length chunk)
    void End();

    bool IsOpen() const { return conn_ && conn_->connected(); }

private:
    void SendHeaders();

    muduo::net::TcpConnectionPtr conn_;
    bool headers_sent_{false};
    int status_code_;
    std::string status_message_;
    std::string content_type_;
};

} // namespace muduo_http

#include "http/mcp/mcp_transport.h"
#include "http/mcp/mcp_session.h"

#include <iostream>
#include <sstream>

namespace muduo_http {
namespace mcp {

McpTransport::McpTransport(muduo::net::EventLoop* loop)
    : loop_(loop),
      session_(std::make_unique<McpSession>()) {}

void McpTransport::OnSseConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sse_conn_ = conn;
        }
        std::cout << "[mcp] SSE connection from " << conn->peerAddress().toIpPort() << '\n';

        // Send the endpoint event so client knows where to POST
        SendSseEvent("endpoint", "/mcp/message");
    } else {
        std::cout << "[mcp] SSE disconnected\n";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sse_conn_.reset();
        }
    }
}

void McpTransport::OnPostMessage(const muduo::net::TcpConnectionPtr& conn,
                                  const std::string& body) {
    // Acknowledge receipt
    conn->send("HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");

    // Forward to message handler
    if (message_cb_) {
        message_cb_(body, *session_);
    }
}

void McpTransport::SendMessage(const std::string& json_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sse_conn_ && sse_conn_->connected()) {
        // Must run in event loop thread
        if (loop_->isInLoopThread()) {
            SendSseEvent("message", json_message);
        } else {
            // Cross-thread: queue in event loop
            auto conn = sse_conn_;
            loop_->queueInLoop([conn, json_message]() {
                std::ostringstream sse;
                sse << "event: message\r\n"
                    << "data: " << json_message << "\r\n\r\n";
                conn->send(sse.str());
            });
        }
    }
}

void McpTransport::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sse_conn_) {
        sse_conn_->shutdown();
        sse_conn_.reset();
    }
}

void McpTransport::SendSseEvent(const std::string& event, const std::string& data) {
    if (!sse_conn_ || !sse_conn_->connected()) return;

    std::ostringstream sse;
    sse << "event: " << event << "\r\n"
        << "data: " << data << "\r\n\r\n";
    sse_conn_->send(sse.str());
}

} // namespace mcp
} // namespace muduo_http

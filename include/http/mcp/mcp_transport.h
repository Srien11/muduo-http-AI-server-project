#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "muduo/net/TcpConnection.h"
#include "muduo/net/EventLoop.h"

namespace muduo_http {
namespace mcp {

class McpSession;

// Callback when a JSON-RPC message is received from client
using MessageCallback = std::function<void(const std::string& raw_message, McpSession& session)>;

class McpTransport {
public:
    explicit McpTransport(muduo::net::EventLoop* loop);

    // Called when a new SSE connection is established (GET /mcp)
    void OnSseConnection(const muduo::net::TcpConnectionPtr& conn);

    // Called when a POST message arrives (POST /mcp/message)
    void OnPostMessage(const muduo::net::TcpConnectionPtr& conn,
                       const std::string& body);

    // Send a JSON-RPC message to the client via SSE
    void SendMessage(const std::string& json_message);

    // Cleanup
    void Close();

    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }

    // Access the session
    McpSession& session() { return *session_; }

private:
    void SendSseEvent(const std::string& event, const std::string& data);

    muduo::net::EventLoop* loop_;
    muduo::net::TcpConnectionPtr sse_conn_;
    std::unique_ptr<McpSession> session_;
    MessageCallback message_cb_;
    std::mutex mutex_;
};

} // namespace mcp
} // namespace muduo_http

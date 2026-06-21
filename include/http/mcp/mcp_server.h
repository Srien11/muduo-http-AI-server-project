#pragma once

#include <memory>
#include <string>
#include <vector>

#include "http/mcp/mcp_protocol.h"
#include "http/mcp/mcp_session.h"
#include "http/mcp/mcp_tool.h"
#include "http/mcp/mcp_transport.h"

#include "muduo/net/EventLoop.h"

namespace muduo_http {
namespace mcp {

class McpServer {
public:
    McpServer(const std::string& name = "muduo_mcp",
              const std::string& version = "0.1.0");

    // Register a tool
    void RegisterTool(const ToolDefinition& def, ToolHandler handler);

    // Access the tool registry (for adding tools later)
    ToolRegistry& tools() { return tool_registry_; }

    // Called when SSE connects (GET /mcp)
    void OnSseConnection(const muduo::net::TcpConnectionPtr& conn);

    // Called when POST message arrives (POST /mcp/message)
    void OnPostMessage(const muduo::net::TcpConnectionPtr& conn,
                       const std::string& body);

    void set_event_loop(muduo::net::EventLoop* loop) { loop_ = loop; }

private:
    // Handle a parsed JSON-RPC request
    void HandleRequest(const JsonRpcRequest& req, McpSession& session);

    // MCP protocol methods
    void HandleInitialize(const JsonRpcRequest& req, McpSession& session);
    void HandleToolsList(const JsonRpcRequest& req, McpSession& session);
    void HandleToolsCall(const JsonRpcRequest& req, McpSession& session);
    void HandleResourcesList(const JsonRpcRequest& req, McpSession& session);
    void HandleResourcesRead(const JsonRpcRequest& req, McpSession& session);
    void HandlePing(const JsonRpcRequest& req, McpSession& session);

    // Find the transport for a given session
    McpTransport* FindTransport(const McpSession& session);

    std::string server_name_;
    std::string server_version_;
    ToolRegistry tool_registry_;
    std::vector<std::unique_ptr<McpTransport>> transports_;
    muduo::net::EventLoop* loop_{nullptr};
};

} // namespace mcp
} // namespace muduo_http

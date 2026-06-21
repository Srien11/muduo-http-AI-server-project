#include "http/mcp/mcp_server.h"

#include <algorithm>
#include <iostream>

namespace muduo_http {
namespace mcp {

McpServer::McpServer(const std::string& name, const std::string& version)
    : server_name_(name), server_version_(version) {}

void McpServer::RegisterTool(const ToolDefinition& def, ToolHandler handler) {
    tool_registry_.Register(def, std::move(handler));
}

void McpServer::OnSseConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
        auto transport = std::make_unique<McpTransport>(loop_);
        transport->set_message_callback(
            [this](const std::string& msg, McpSession& session) {
                JsonRpcRequest req;
                if (McpProtocol::ParseRequest(msg, req)) {
                    HandleRequest(req, session);
                } else {
                    std::cerr << "[mcp] failed to parse: " << msg.substr(0, 100) << '\n';
                    // Can't respond because we don't have a transport reference directly
                }
            });
        transport->OnSseConnection(conn);
        transports_.push_back(std::move(transport));
    } else {
        // Remove disconnected transport
        transports_.erase(
            std::remove_if(transports_.begin(), transports_.end(),
                [&conn](const auto& t) {
                    return false; // Simplified: would need session tracking
                }),
            transports_.end());
    }
}

void McpServer::OnPostMessage(const muduo::net::TcpConnectionPtr& conn,
                               const std::string& body) {
    // Parse the incoming message
    JsonRpcRequest req;
    if (!McpProtocol::ParseRequest(body, req)) {
        std::string err = McpProtocol::MakeError("", ErrorCode::kParseError,
                                                   "Parse error");
        conn->send("HTTP/1.1 400 Bad Request\r\nContent-Length:" +
                   std::to_string(err.size()) + "\r\n\r\n" + err);
        return;
    }

    // For now, handle via the first transport
    // Simplified: in production, identify session from connection
    if (!transports_.empty()) {
        HandleRequest(req, transports_[0]->session());
    }
}

void McpServer::HandleRequest(const JsonRpcRequest& req, McpSession& session) {
    // Handle by method
    if (req.method == "initialize") {
        HandleInitialize(req, session);
    } else if (req.method == "ping") {
        HandlePing(req, session);
    } else if (req.method == "tools/list") {
        HandleToolsList(req, session);
    } else if (req.method == "tools/call") {
        HandleToolsCall(req, session);
    } else if (req.method == "resources/list") {
        HandleResourcesList(req, session);
    } else if (req.method == "resources/read") {
        HandleResourcesRead(req, session);
    } else if (req.method == "notifications/initialized") {
        session.SetInitialized();
        std::cout << "[mcp] client initialized: " << session.client_name << '\n';
    } else if (req.method == "logging/setLevel") {
        // Acknowledge silently
    } else {
        // Unknown method
        if (!req.is_notification) {
            auto* transport = FindTransport(session);
            if (transport) {
                transport->SendMessage(
                    McpProtocol::MakeError(req.id, ErrorCode::kMethodNotFound,
                                            "Method not found: " + req.method));
            }
        }
    }
}

void McpServer::HandleInitialize(const JsonRpcRequest& req, McpSession& session) {
    // Extract client info
    if (req.params.contains("clientInfo")) {
        auto& info = req.params["clientInfo"];
        session.client_name = info.value("name", "unknown");
        session.client_version = info.value("version", "0.0");
    }
    session.protocol_version = req.params.value("protocolVersion", "2024-11-05");
    session.client_caps.tools = req.params["capabilities"].value("tools", false);

    // Mark state
    session.SetInitialized();

    // Build response
    nlohmann::json result = {
        {"protocolVersion", "2024-11-05"},
        {"serverInfo", {
            {"name", server_name_},
            {"version", server_version_}
        }},
        {"capabilities", {
            {"tools", session.server_caps.tools},
            {"resources", session.server_caps.resources},
            {"logging", session.server_caps.logging}
        }}
    };

    auto* transport = FindTransport(session);
    if (transport) {
        transport->SendMessage(McpProtocol::MakeResponse(req.id, result));
        std::cout << "[mcp] initialized: " << session.client_name
                  << " v" << session.client_version << '\n';
    }
}

void McpServer::HandleToolsList(const JsonRpcRequest& req, McpSession& session) {
    auto tools = tool_registry_.ListTools();
    nlohmann::json tools_json = nlohmann::json::array();
    for (const auto& tool : tools) {
        tools_json.push_back(tool.ToJson());
    }

    nlohmann::json result = {{"tools", tools_json}};

    auto* transport = FindTransport(session);
    if (transport) {
        transport->SendMessage(McpProtocol::MakeResponse(req.id, result));
    }
}

void McpServer::HandleToolsCall(const JsonRpcRequest& req, McpSession& session) {
    std::string name = req.params.value("name", "");
    nlohmann::json args = req.params.value("arguments", nlohmann::json::object());

    if (name.empty()) {
        auto* transport = FindTransport(session);
        if (transport) {
            transport->SendMessage(
                McpProtocol::MakeError(req.id, ErrorCode::kInvalidParams,
                                        "Missing tool name"));
        }
        return;
    }

    ToolResult result = tool_registry_.CallTool(name, args);

    nlohmann::json content_json = nlohmann::json::array();
    for (const auto& c : result.content) {
        content_json.push_back(c);
    }

    nlohmann::json resp = {
        {"content", content_json},
        {"isError", result.is_error}
    };

    auto* transport = FindTransport(session);
    if (transport) {
        transport->SendMessage(McpProtocol::MakeResponse(req.id, resp));
    }
}

void McpServer::HandleResourcesList(const JsonRpcRequest& req, McpSession& session) {
    // Minimal: return empty list
    nlohmann::json result = {{"resources", nlohmann::json::array()}};
    auto* transport = FindTransport(session);
    if (transport) {
        transport->SendMessage(McpProtocol::MakeResponse(req.id, result));
    }
}

void McpServer::HandleResourcesRead(const JsonRpcRequest& req, McpSession& session) {
    auto* transport = FindTransport(session);
    if (transport) {
        transport->SendMessage(
            McpProtocol::MakeError(req.id, ErrorCode::kInvalidParams,
                                    "Resource reading not implemented"));
    }
}

void McpServer::HandlePing(const JsonRpcRequest& req, McpSession& session) {
    auto* transport = FindTransport(session);
    if (transport) {
        transport->SendMessage(
            McpProtocol::MakeResponse(req.id, nlohmann::json::object()));
    }
}

McpTransport* McpServer::FindTransport(const McpSession& session) {
    // Simplified: find by session match.
    // In multi-client scenario, would need proper session-to-transport mapping.
    // Using raw pointer comparison.
    for (auto& t : transports_) {
        if (&t->session() == &session) {
            return t.get();
        }
    }
    return nullptr;
}

} // namespace mcp
} // namespace muduo_http

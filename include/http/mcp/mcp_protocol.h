#pragma once

#include <string>
#include <vector>

#include "http/mcp/json.hpp"

namespace muduo_http {
namespace mcp {

// JSON-RPC 2.0 message types
struct JsonRpcRequest {
    std::string id;          // Message ID (empty for notifications)
    std::string method;
    nlohmann::json params;
    bool is_notification = false;  // true if no "id" field
};

struct JsonRpcResponse {
    std::string id;
    nlohmann::json result;
};

struct JsonRpcError {
    std::string id;
    int code;
    std::string message;
    nlohmann::json data;
};

// Standard JSON-RPC error codes
enum ErrorCode {
    kParseError = -32700,
    kInvalidRequest = -32600,
    kMethodNotFound = -32601,
    kInvalidParams = -32602,
    kInternalError = -32603,
    // MCP specific
    kToolNotFound = -32000,
    kToolExecutionError = -32001,
};

class McpProtocol {
public:
    // Parse raw JSON string into a typed message
    static bool ParseRequest(const std::string& raw, JsonRpcRequest& req);

    // Build response
    static std::string MakeResponse(const std::string& id, const nlohmann::json& result);
    static std::string MakeError(const std::string& id, int code,
                                  const std::string& message,
                                  const nlohmann::json& data = nullptr);
    static std::string MakeNotification(const std::string& method,
                                         const nlohmann::json& params);

    // Helper: make success result content
    static nlohmann::json TextContent(const std::string& text,
                                       const std::string& mime_type = "text/plain");
};

} // namespace mcp
} // namespace muduo_http

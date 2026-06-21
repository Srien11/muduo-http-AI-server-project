#include "http/mcp/mcp_protocol.h"

namespace muduo_http {
namespace mcp {

bool McpProtocol::ParseRequest(const std::string& raw, JsonRpcRequest& req) {
    try {
        auto json = nlohmann::json::parse(raw);

        // Must have jsonrpc: "2.0"
        if (json.value("jsonrpc", "") != "2.0") return false;

        // Must have method
        if (!json.contains("method")) return false;
        req.method = json["method"];

        // Optional id - if absent, it's a notification
        if (json.contains("id")) {
            // Normalize id to string
            auto& id_val = json["id"];
            if (id_val.is_string()) req.id = id_val;
            else if (id_val.is_number()) req.id = std::to_string(id_val.get<long long>());
            else req.id = id_val.dump();
            req.is_notification = false;
        } else {
            req.is_notification = true;
        }

        // Optional params
        if (json.contains("params")) {
            req.params = json["params"];
        }

        return true;
    } catch (...) {
        return false;
    }
}

std::string McpProtocol::MakeResponse(const std::string& id, const nlohmann::json& result) {
    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
    return msg.dump();
}

std::string McpProtocol::MakeError(const std::string& id, int code,
                                    const std::string& message,
                                    const nlohmann::json& data) {
    nlohmann::json err = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
    if (!data.is_null()) {
        err["error"]["data"] = data;
    }
    return err.dump();
}

std::string McpProtocol::MakeNotification(const std::string& method,
                                           const nlohmann::json& params) {
    nlohmann::json msg = {
        {"jsonrpc", "2.0"},
        {"method", method}
    };
    if (!params.is_null()) {
        msg["params"] = params;
    }
    return msg.dump();
}

nlohmann::json McpProtocol::TextContent(const std::string& text,
                                         const std::string& mime_type) {
    return {
        {"type", "text"},
        {"text", text},
        {"mimeType", mime_type}
    };
}

} // namespace mcp
} // namespace muduo_http

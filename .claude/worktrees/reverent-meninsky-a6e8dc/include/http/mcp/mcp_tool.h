#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "http/mcp/json.hpp"

namespace muduo_http {
namespace mcp {

// Single parameter definition (JSON Schema)
struct ToolParam {
    std::string name;
    std::string description;
    std::string type = "string";  // string, number, boolean, array, object
    bool required = false;
    std::vector<std::string> enum_values;
};

// Tool definition
struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<ToolParam> parameters;

    nlohmann::json ToJson() const;

    // Convert to OpenAI tool format (for API calls)
    nlohmann::json ToOpenAITool() const;
};

// Tool execution result
struct ToolResult {
    bool success = true;
    std::vector<nlohmann::json> content;  // List of content items
    bool is_error = false;
};

// Tool handler: receives parsed arguments, returns result
using ToolHandler = std::function<ToolResult(const nlohmann::json& args)>;

class ToolRegistry {
public:
    void Register(const ToolDefinition& def, ToolHandler handler);
    bool Unregister(const std::string& name);
    bool HasTool(const std::string& name) const;

    // Get tool definition (for tools/list)
    ToolDefinition GetTool(const std::string& name) const;
    std::vector<ToolDefinition> ListTools() const;

    // Execute tool (for tools/call)
    ToolResult CallTool(const std::string& name, const nlohmann::json& args);

private:
    struct ToolEntry {
        ToolDefinition def;
        ToolHandler handler;
    };
    std::unordered_map<std::string, ToolEntry> tools_;
};

} // namespace mcp
} // namespace muduo_http

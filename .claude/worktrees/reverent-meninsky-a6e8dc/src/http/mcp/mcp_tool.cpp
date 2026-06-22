#include "http/mcp/mcp_tool.h"

namespace muduo_http {
namespace mcp {

nlohmann::json ToolDefinition::ToJson() const {
    nlohmann::json schema = nlohmann::json::object();
    schema["type"] = "object";

    nlohmann::json props = nlohmann::json::object();
    nlohmann::json required_arr = nlohmann::json::array();

    for (const auto& param : parameters) {
        nlohmann::json p = {
            {"type", param.type},
            {"description", param.description}
        };
        if (!param.enum_values.empty()) {
            p["enum"] = param.enum_values;
        }
        props[param.name] = p;
        if (param.required) {
            required_arr.push_back(param.name);
        }
    }

    schema["properties"] = props;
    if (!required_arr.empty()) {
        schema["required"] = required_arr;
    }

    return {
        {"name", name},
        {"description", description},
        {"inputSchema", schema}
    };
}

nlohmann::json ToolDefinition::ToOpenAITool() const {
    auto mcp_json = ToJson();
    return {
        {"type", "function"},
        {"function", {
            {"name", mcp_json["name"]},
            {"description", mcp_json["description"]},
            {"parameters", mcp_json["inputSchema"]}
        }}
    };
}

void ToolRegistry::Register(const ToolDefinition& def, ToolHandler handler) {
    tools_[def.name] = {def, std::move(handler)};
}

bool ToolRegistry::Unregister(const std::string& name) {
    return tools_.erase(name) > 0;
}

bool ToolRegistry::HasTool(const std::string& name) const {
    return tools_.find(name) != tools_.end();
}

ToolDefinition ToolRegistry::GetTool(const std::string& name) const {
    auto it = tools_.find(name);
    if (it != tools_.end()) return it->second.def;
    return {};
}

std::vector<ToolDefinition> ToolRegistry::ListTools() const {
    std::vector<ToolDefinition> result;
    result.reserve(tools_.size());
    for (const auto& [name, entry] : tools_) {
        result.push_back(entry.def);
    }
    return result;
}

ToolResult ToolRegistry::CallTool(const std::string& name, const nlohmann::json& args) {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        ToolResult err;
        err.success = false;
        err.is_error = true;
        err.content.push_back({{"type", "text"}, {"text", "tool not found: " + name}});
        return err;
    }
    return it->second.handler(args);
}

} // namespace mcp
} // namespace muduo_http

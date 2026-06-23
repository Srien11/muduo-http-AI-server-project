#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "http/ai_config.h"
#include "http/ai_gateway.h"
#include "http/mcp/mcp_tool.h"

namespace muduo_http {

// Tool execution function: takes tool name and JSON arguments, returns text result
using ToolExecutor = std::function<std::string(const std::string& tool_name,
                                                const std::string& arguments)>;

class ChatAgent {
public:
    ChatAgent(std::shared_ptr<AiGateway> gateway,
              ToolExecutor executor,
              const std::string& system_prompt = "");

    // Process a user message through the LLM tool loop
    // Returns the final natural language response
    AiChatResponse Process(const std::string& user_message);

    // Clear conversation history
    void ClearHistory();

    // Persist history to file
    bool SaveHistory(const std::string& filepath) const;
    bool LoadHistory(const std::string& filepath);

    // Access conversation history
    std::vector<AiChatMessage>& history() { return history_; }
    const std::vector<AiChatMessage>& history() const { return history_; }

    // Get the current session ID
    const std::string& session_id() const { return session_id_; }
    void set_session_id(const std::string& id) { session_id_ = id; }

    // Set available tools (JSON array of tool definitions for OpenAI API)
    void SetTools(const nlohmann::json& tools);

    // Dynamic prompt support
    void set_system_prompt(const std::string& prompt) { system_prompt_ = prompt; }
    const std::string& system_prompt() const { return system_prompt_; }
    const ToolExecutor& tool_executor() const { return tool_executor_; }

private:
    std::shared_ptr<AiGateway> gateway_;
    ToolExecutor tool_executor_;
    std::vector<AiChatMessage> history_;
    std::string session_id_;
    nlohmann::json tools_;
    std::string system_prompt_;
    int max_tool_calls_{10};  // Prevent infinite loops
};

} // namespace muduo_http

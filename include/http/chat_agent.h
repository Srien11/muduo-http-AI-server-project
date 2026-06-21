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

    // Access conversation history
    std::vector<AiChatMessage>& history() { return history_; }

    // Set available tools (JSON array of tool definitions for OpenAI API)
    void SetTools(const nlohmann::json& tools);

private:
    std::shared_ptr<AiGateway> gateway_;
    ToolExecutor tool_executor_;
    std::vector<AiChatMessage> history_;
    nlohmann::json tools_;
    std::string system_prompt_;
    int max_tool_calls_{10};  // Prevent infinite loops
};

} // namespace muduo_http

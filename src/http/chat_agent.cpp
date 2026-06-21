#include "http/chat_agent.h"

#include <iostream>

namespace muduo_http {

ChatAgent::ChatAgent(std::shared_ptr<AiGateway> gateway,
                     ToolExecutor executor,
                     const std::string& system_prompt)
    : gateway_(std::move(gateway)),
      tool_executor_(std::move(executor)),
      system_prompt_(system_prompt) {}

AiChatResponse ChatAgent::Process(const std::string& user_message) {
    // Add user message to history
    history_.push_back({"user", user_message});

    int tool_calls = 0;
    AiChatResponse final_response;

    while (tool_calls < max_tool_calls_) {
        // Build request with full history + tools
        AiChatRequest req;
        if (!system_prompt_.empty()) {
            req.messages.push_back({"system", system_prompt_});
        }
        req.messages.insert(req.messages.end(), history_.begin(), history_.end());
        req.tools = tools_;

        // Call LLM
        auto response = gateway_->Chat(req);
        if (!response.success) {
            final_response = response;
            break;
        }

        // If LLM wants to call tools
        if (response.has_tool_calls()) {
            // Add assistant message with tool_calls to history
            // For now, we store the content (which might be empty) and append tool call info
            std::string assistant_content = response.content;
            history_.push_back({"assistant", assistant_content});

            // Execute each tool call
            for (auto& tc : response.tool_calls) {
                std::cout << "[agent] calling tool: " << tc.name
                          << "(" << tc.arguments.substr(0, 100) << "...)\n";

                // Execute tool
                std::string tool_result = tool_executor_(tc.name, tc.arguments);

                // Add tool result to history
                AiChatMessage tool_msg;
                tool_msg.role = "tool";
                tool_msg.tool_call_id = tc.id;
                tool_msg.name = tc.name;
                tool_msg.content = tool_result;
                history_.push_back(tool_msg);
            }

            tool_calls++;

            // If LLM also returned text content alongside tool calls, use it
            if (!response.content.empty() && !response.has_tool_calls()) {
                final_response = response;
                break;
            }
            // Otherwise loop again to let LLM process tool results
        } else {
            // LLM returned a natural language response
            final_response = response;
            history_.push_back({"assistant", response.content});
            break;
        }
    }

    if (tool_calls >= max_tool_calls_) {
        final_response.content = "Reached maximum tool call limit.";
        history_.push_back({"assistant", final_response.content});
    }

    return final_response;
}

void ChatAgent::ClearHistory() {
    history_.clear();
}

void ChatAgent::SetTools(const nlohmann::json& tools) {
    tools_ = tools;
}

} // namespace muduo_http

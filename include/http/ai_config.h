#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "http/mcp/json.hpp"

namespace muduo_http {

struct AiConfig {
    // API endpoint
    std::string api_base = "https://api.openai.com/v1";
    std::string api_key;
    std::string model = "gpt-3.5-turbo";

    // Request limits
    int max_tokens = 2048;
    double temperature = 0.7;

    // Rate limiting
    int rate_limit_rps = 10;         // Requests per second
    int rate_limit_burst = 20;       // Burst size

    // Caching
    bool cache_enabled = true;
    int cache_ttl_seconds = 300;     // 5 min
    size_t cache_max_entries = 1000;

    // Connection
    int timeout_seconds = 60;
};

struct AiChatMessage {
    std::string role;       // "system", "user", "assistant", "tool"
    std::string content;
    std::string tool_call_id;    // for tool responses
    std::string name;           // for tool responses
    std::string reasoning_content;  // DeepSeek thinking mode reasoning (must be passed back)
    nlohmann::json tool_calls;   // for assistant messages with tool_calls
};

struct AiChatRequest {
    std::vector<AiChatMessage> messages;
    std::string model;
    int max_tokens = 2048;
    double temperature = 0.7;
    bool stream = false;
    nlohmann::json tools;       // JSON array of tool definitions
};

struct AiChatResponse {
    bool success = false;
    std::string content;
    std::string reasoning_content;  // DeepSeek thinking mode reasoning (must be passed back)
    std::string error_message;
    int prompt_tokens = 0;
    int completion_tokens = 0;
    // Tool calls from LLM
    struct ToolCall {
        std::string id;
        std::string name;
        std::string arguments;  // JSON string
    };
    std::vector<ToolCall> tool_calls;
    bool has_tool_calls() const { return !tool_calls.empty(); }
};

} // namespace muduo_http

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

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
    std::string role;       // "system", "user", "assistant"
    std::string content;
};

struct AiChatRequest {
    std::vector<AiChatMessage> messages;
    std::string model;
    int max_tokens = 2048;
    double temperature = 0.7;
    bool stream = false;
};

struct AiChatResponse {
    bool success = false;
    std::string content;
    std::string error_message;
    int prompt_tokens = 0;
    int completion_tokens = 0;
};

} // namespace muduo_http

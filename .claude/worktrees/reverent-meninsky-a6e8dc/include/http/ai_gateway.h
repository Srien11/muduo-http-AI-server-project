#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai_config.h"
#include "rate_limiter.h"
#include "stream_writer.h"

namespace muduo_http {

// Simple LRU cache for chat responses
class AiResponseCache {
public:
    explicit AiResponseCache(size_t max_entries, int ttl_seconds);

    bool Get(const std::string& key, std::string& value);
    void Set(const std::string& key, const std::string& value);
    void Clear();

private:
    struct Entry {
        std::string value;
        std::chrono::steady_clock::time_point created_at;
    };

    size_t max_entries_;
    int ttl_seconds_;
    std::unordered_map<std::string, Entry> cache_;
    std::mutex mutex_;
};

// AI Gateway: rate-limited, cached proxy to OpenAI-compatible APIs
class AiGateway {
public:
    explicit AiGateway(AiConfig config);

    // Synchronous chat completion
    AiChatResponse Chat(const AiChatRequest& request);

    // Streaming chat completion (writes SSE events to StreamWriter)
    void ChatStream(const AiChatRequest& request, StreamWriter& writer);

    // Configuration
    const AiConfig& config() const { return config_; }
    void UpdateConfig(const AiConfig& config) { config_ = config; }
    void SetApiKey(const std::string& key) { config_.api_key = key; }
    void SetModel(const std::string& model) { config_.model = model; }
    void SetApiBase(const std::string& base) { config_.api_base = base; }

    // Rate limiter stats
    int rate_limit_remaining() const;

private:
    std::string BuildRequestBody(const AiChatRequest& request);
    AiChatResponse ParseResponse(const std::string& body);
    std::string BuildCacheKey(const AiChatRequest& request);
    std::string UrlEncode(const std::string& value);

    AiConfig config_;
    RateLimiter rate_limiter_;
    std::unique_ptr<AiResponseCache> cache_;
};

} // namespace muduo_http

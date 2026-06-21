#pragma once

#include <memory>
#include <string>
#include <vector>

#include "http/ai_config.h"
#include "http/ai_gateway.h"

namespace muduo_http {

// -------- Circuit Breaker --------
enum class CircuitState { kClosed, kOpen, kHalfOpen };

class CircuitBreaker {
public:
    CircuitBreaker(int failure_threshold = 5,
                   int recovery_timeout_seconds = 30,
                   int half_open_max = 1);

    // Call before a request: returns true if allowed
    bool AllowRequest();

    // Call after a successful request
    void OnSuccess();

    // Call after a failed request
    void OnFailure();

    CircuitState state() const { return state_; }
    std::string StateString() const;

private:
    CircuitState state_{CircuitState::kClosed};
    int failure_count_{0};
    int failure_threshold_;
    int recovery_timeout_seconds_;
    int half_open_max_;
    int half_open_requests_{0};
    std::chrono::steady_clock::time_point last_failure_time_;
    mutable std::mutex mutex_;
};

// -------- Provider Configuration --------
struct ProviderConfig {
    std::string name;           // "openai", "claude", etc.
    std::string api_base;
    std::string api_key;
    std::string model;
    int timeout_seconds = 60;
    int weight = 100;           // Higher = more likely to be selected (for weighted routing)
    bool enabled = true;
};

// -------- Model Router --------
class ModelRouter {
public:
    ModelRouter() = default;

    // Add a provider
    void AddProvider(const ProviderConfig& config);
    void AddProvider(const ProviderConfig& config, std::shared_ptr<AiGateway> gateway);

    // Route a chat request to the best available provider
    AiChatResponse Chat(const AiChatRequest& request);

    // List all providers and their status
    struct ProviderStatus {
        std::string name;
        std::string model;
        std::string state;  // "open", "half-open", "open"
        bool enabled;
    };
    std::vector<ProviderStatus> GetStatus() const;

private:
    struct Provider {
        ProviderConfig config;
        std::shared_ptr<AiGateway> gateway;
        std::unique_ptr<CircuitBreaker> breaker;
    };

    Provider* SelectProvider();
    std::vector<Provider> providers_;
    int round_robin_index_{0};
};

} // namespace muduo_http

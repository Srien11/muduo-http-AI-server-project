#include "http/model_router.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>

namespace muduo_http {

// -------- CircuitBreaker --------

CircuitBreaker::CircuitBreaker(int failure_threshold,
                               int recovery_timeout_seconds,
                               int half_open_max)
    : failure_threshold_(failure_threshold),
      recovery_timeout_seconds_(recovery_timeout_seconds),
      half_open_max_(half_open_max) {}

bool CircuitBreaker::AllowRequest() {
    std::lock_guard<std::mutex> lock(mutex_);

    switch (state_) {
        case CircuitState::kClosed:
            return true;

        case CircuitState::kOpen: {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_failure_time_).count();
            if (elapsed >= recovery_timeout_seconds_) {
                state_ = CircuitState::kHalfOpen;
                half_open_requests_ = 0;
                return true;
            }
            return false;
        }

        case CircuitState::kHalfOpen:
            if (half_open_requests_ < half_open_max_) {
                half_open_requests_++;
                return true;
            }
            return false;
    }
    return false;
}

void CircuitBreaker::OnSuccess() {
    std::lock_guard<std::mutex> lock(mutex_);
    failure_count_ = 0;
    if (state_ == CircuitState::kHalfOpen) {
        state_ = CircuitState::kClosed;
        half_open_requests_ = 0;
    }
}

void CircuitBreaker::OnFailure() {
    std::lock_guard<std::mutex> lock(mutex_);
    failure_count_++;
    last_failure_time_ = std::chrono::steady_clock::now();

    if (state_ == CircuitState::kHalfOpen) {
        state_ = CircuitState::kOpen;
    } else if (failure_count_ >= failure_threshold_) {
        state_ = CircuitState::kOpen;
    }
}

std::string CircuitBreaker::StateString() const {
    switch (state_) {
        case CircuitState::kClosed:   return "closed";
        case CircuitState::kOpen:     return "open";
        case CircuitState::kHalfOpen: return "half-open";
    }
    return "unknown";
}

// -------- ModelRouter --------

void ModelRouter::AddProvider(const ProviderConfig& config) {
    AiConfig ai_cfg;
    ai_cfg.api_base = config.api_base;
    ai_cfg.api_key = config.api_key;
    ai_cfg.model = config.model;
    ai_cfg.timeout_seconds = config.timeout_seconds;

    auto gateway = std::make_shared<AiGateway>(ai_cfg);
    providers_.push_back({config, gateway,
                          std::make_unique<CircuitBreaker>()});
}

void ModelRouter::AddProvider(const ProviderConfig& config,
                               std::shared_ptr<AiGateway> gateway) {
    providers_.push_back({config, std::move(gateway),
                          std::make_unique<CircuitBreaker>()});
}

AiChatResponse ModelRouter::Chat(const AiChatRequest& request) {
    if (providers_.empty()) {
        AiChatResponse err;
        err.error_message = "no providers configured";
        return err;
    }

    // Try providers in priority order (weighted)
    // Collect all available providers first
    std::vector<Provider*> candidates;
    for (auto& p : providers_) {
        if (p.config.enabled) {
            candidates.push_back(&p);
        }
    }

    if (candidates.empty()) {
        AiChatResponse err;
        err.error_message = "all providers disabled";
        return err;
    }

    // Sort by weight descending (higher weight = higher priority)
    std::sort(candidates.begin(), candidates.end(),
              [](const Provider* a, const Provider* b) {
                  return a->config.weight > b->config.weight;
              });

    // Try each provider until one succeeds
    for (auto* provider : candidates) {
        if (!provider->breaker->AllowRequest()) {
            std::cout << "[router] " << provider->config.name
                      << " circuit open, skipping\n";
            continue;
        }

        // Build request with this provider's model if not specified
        AiChatRequest routed_req = request;
        if (routed_req.model.empty()) {
            routed_req.model = provider->config.model;
        }

        auto result = provider->gateway->Chat(routed_req);
        if (result.success) {
            provider->breaker->OnSuccess();
            return result;
        }

        provider->breaker->OnFailure();
        std::cout << "[router] " << provider->config.name
                  << " failed: " << result.error_message
                  << ", triage to next provider\n";
    }

    AiChatResponse err;
    err.error_message = "all providers failed";
    return err;
}

std::vector<ModelRouter::ProviderStatus> ModelRouter::GetStatus() const {
    std::vector<ProviderStatus> status;
    for (const auto& p : providers_) {
        status.push_back({
            p.config.name,
            p.config.model,
            p.breaker->StateString(),
            p.config.enabled
        });
    }
    return status;
}

} // namespace muduo_http

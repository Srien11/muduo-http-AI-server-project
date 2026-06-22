#pragma once

#include <chrono>
#include <mutex>

namespace muduo_http {

// Token bucket rate limiter
class RateLimiter {
public:
    RateLimiter(double rate_per_second, int burst);

    // Returns true if request is allowed, false if rate limited
    bool Allow();

    // Reset the counter
    void Reset();

private:
    double rate_;
    double burst_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    std::mutex mutex_;
};

} // namespace muduo_http

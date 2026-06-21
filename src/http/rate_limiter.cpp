#include "http/rate_limiter.h"

#include <algorithm>

namespace muduo_http {

RateLimiter::RateLimiter(double rate_per_second, int burst)
    : rate_(rate_per_second),
      burst_(static_cast<double>(burst)),
      tokens_(static_cast<double>(burst)),
      last_refill_(std::chrono::steady_clock::now()) {}

bool RateLimiter::Allow() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_refill_).count();
    last_refill_ = now;

    // Refill tokens based on elapsed time
    tokens_ = std::min(burst_, tokens_ + elapsed * rate_);

    if (tokens_ >= 1.0) {
        tokens_ -= 1.0;
        return true;
    }

    return false;
}

void RateLimiter::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_ = burst_;
    last_refill_ = std::chrono::steady_clock::now();
}

} // namespace muduo_http

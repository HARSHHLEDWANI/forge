#include "server/rate_limiter.hpp"

#include <algorithm>

namespace forge::server {

RateLimiter::RateLimiter(std::size_t capacity, double refill_per_second)
    : capacity_(capacity), refill_per_second_(refill_per_second) {}

bool RateLimiter::allow(const std::string& key) {
    const auto now = std::chrono::steady_clock::now();
    auto [it, inserted] = buckets_.try_emplace(key, Bucket{static_cast<double>(capacity_), now});
    Bucket& bucket = it->second;

    if (!inserted) {
        const double elapsed_seconds = std::chrono::duration<double>(now - bucket.last_refill).count();
        bucket.tokens = std::min(static_cast<double>(capacity_), bucket.tokens + elapsed_seconds * refill_per_second_);
        bucket.last_refill = now;
    }

    if (bucket.tokens < 1.0) {
        return false;
    }
    bucket.tokens -= 1.0;
    return true;
}

} // namespace forge::server

#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <string>

namespace forge::server {

// Token-bucket rate limiter keyed by client IP. Guards against
// credential-stuffing / brute-force login attempts (POST /login is the
// one route this project puts it on — see server/app.cpp). No internal
// locking: HttpServer's accept loop is single-threaded by design
// (Phase 12), so allow() is never called concurrently.
//
// Each key gets `capacity` tokens, refilled continuously at
// `refill_per_second` tokens/sec, capped at `capacity`. allow() debits
// one token and returns whether the request may proceed.
class RateLimiter {
public:
    RateLimiter(std::size_t capacity, double refill_per_second);

    bool allow(const std::string& key);

private:
    struct Bucket {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
    };

    std::size_t capacity_;
    double refill_per_second_;
    std::map<std::string, Bucket> buckets_;
};

} // namespace forge::server

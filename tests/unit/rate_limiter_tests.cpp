#include <chrono>
#include <thread>

#include "server/rate_limiter.hpp"
#include "support/test_framework.hpp"

using forge::server::RateLimiter;

FORGE_TEST_CASE(rate_limiter_allows_up_to_capacity_then_rejects) {
    RateLimiter limiter(3, 0.0); // no refill: isolates the "capacity" behavior from the "refill" behavior
    FORGE_CHECK(limiter.allow("1.2.3.4"));
    FORGE_CHECK(limiter.allow("1.2.3.4"));
    FORGE_CHECK(limiter.allow("1.2.3.4"));
    FORGE_CHECK(!limiter.allow("1.2.3.4"));
}

FORGE_TEST_CASE(rate_limiter_tracks_each_key_independently) {
    RateLimiter limiter(1, 0.0);
    FORGE_CHECK(limiter.allow("1.2.3.4"));
    FORGE_CHECK(!limiter.allow("1.2.3.4"));
    FORGE_CHECK(limiter.allow("5.6.7.8")); // a different key starts with its own full bucket
}

FORGE_TEST_CASE(rate_limiter_refills_over_time) {
    RateLimiter limiter(1, 1000.0); // fast refill so the test doesn't need to sleep long
    FORGE_CHECK(limiter.allow("1.2.3.4"));
    FORGE_CHECK(!limiter.allow("1.2.3.4"));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    FORGE_CHECK(limiter.allow("1.2.3.4"));
}

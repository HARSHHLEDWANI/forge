#include "core/object_id.hpp"

#include <algorithm>
#include <cstring>

namespace forge::core {

namespace {

constexpr std::string_view kHexDigits = "0123456789abcdef";

std::optional<int> hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return std::nullopt;
}

std::optional<std::byte> hex_pair_to_byte(char high, char low) {
    const auto h = hex_nibble(high);
    const auto l = hex_nibble(low);
    if (!h || !l) {
        return std::nullopt;
    }
    return static_cast<std::byte>((*h << 4) | *l);
}

} // namespace

ObjectId ObjectId::of(std::string_view canonical_bytes) {
    return ObjectId(sha256(canonical_bytes));
}

std::optional<ObjectId> ObjectId::parse(std::string_view hex) {
    if (hex.size() != kSize * 2) {
        return std::nullopt;
    }

    Sha256Digest digest{};
    for (std::size_t i = 0; i < kSize; ++i) {
        const auto byte = hex_pair_to_byte(hex[i * 2], hex[i * 2 + 1]);
        if (!byte) {
            return std::nullopt;
        }
        digest[i] = *byte;
    }
    return ObjectId(digest);
}

std::string ObjectId::to_hex() const {
    std::string result;
    result.reserve(kSize * 2);
    for (std::byte b : digest_) {
        const auto value = std::to_integer<unsigned int>(b);
        result.push_back(kHexDigits[(value >> 4) & 0xF]);
        result.push_back(kHexDigits[value & 0xF]);
    }
    return result;
}

} // namespace forge::core

std::size_t std::hash<forge::core::ObjectId>::operator()(const forge::core::ObjectId& id) const noexcept {
    // The digest is already a uniformly-distributed cryptographic hash;
    // folding its leading bytes is enough entropy for a bucket index and
    // avoids scanning all 32 bytes on every lookup.
    std::size_t seed = 0;
    std::memcpy(&seed, id.bytes().data(), std::min(sizeof(seed), id.bytes().size()));
    return seed;
}

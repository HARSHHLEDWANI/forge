#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/hashing.hpp"

namespace forge::core {

// Content identity for a Forge object: SHA-256 of its canonical bytes
// (see object_encoding.hpp). Immutable, comparable, and hashable — safe
// to use as a map key once the object store needs one.
class ObjectId {
public:
    static constexpr std::size_t kSize = 32;

    explicit ObjectId(Sha256Digest digest) : digest_(digest) {}

    static ObjectId of(std::string_view canonical_bytes);
    static std::optional<ObjectId> parse(std::string_view hex);

    std::string to_hex() const;
    const Sha256Digest& bytes() const { return digest_; }

    friend bool operator==(const ObjectId&, const ObjectId&) = default;

private:
    Sha256Digest digest_;
};

} // namespace forge::core

template <>
struct std::hash<forge::core::ObjectId> {
    std::size_t operator()(const forge::core::ObjectId& id) const noexcept;
};

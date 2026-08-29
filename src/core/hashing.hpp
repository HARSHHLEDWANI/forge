#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

namespace forge::core {

using Sha256Digest = std::array<std::byte, 32>;

// Incremental SHA-256, for hashing data too large to hold in memory at
// once (see storage/atomic_file.hpp's binary-safety conventions: content
// is treated as raw bytes via string_view throughout). One-shot callers
// should prefer sha256().
//
// Backed by OpenSSL (EVP) rather than a hand-rolled implementation —
// cryptographic primitives use an audited library, never a bespoke one.
class Sha256Hasher {
public:
    Sha256Hasher();
    ~Sha256Hasher();
    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;
    Sha256Hasher(Sha256Hasher&&) noexcept;
    Sha256Hasher& operator=(Sha256Hasher&&) noexcept;

    void update(std::string_view data);
    Sha256Digest finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Sha256Digest sha256(std::string_view data);

} // namespace forge::core

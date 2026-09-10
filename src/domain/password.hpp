#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace forge::domain {

// PBKDF2-HMAC-SHA256, OWASP's current minimum-recommended iteration
// count for it (600,000) as of this writing, via OpenSSL's PKCS5_PBKDF2_HMAC
// — reusing the OpenSSL::Crypto dependency this project already has for
// SHA-256 (core/hashing.hpp) rather than adding a new one (e.g. Argon2)
// for what a threat-modeling pass doesn't demand here: this project has
// no adversary capable of the offline, purpose-built hardware attack
// PBKDF2's lack of memory-hardness matters against, and "reuse an
// existing dependency" beats "add a better one" until it doesn't.
struct PasswordHash {
    std::string algorithm = "pbkdf2-hmac-sha256";
    unsigned int iterations = 600000;
    std::string salt_hex;
    std::string hash_hex;

    friend bool operator==(const PasswordHash&, const PasswordHash&) = default;
};

// Derives a PasswordHash from `password` with a fresh random salt (16
// bytes, via OpenSSL's RAND_bytes). Throws core::ForgeError if
// `password` is empty — an empty password is never a legitimate
// credential, and the KDF itself doesn't reject one on its own.
PasswordHash hash_password(std::string_view password);

// Recomputes the KDF using `stored`'s own algorithm/iterations/salt and
// compares in constant time (OpenSSL's CRYPTO_memcmp) — a plain `==` on
// secret-derived bytes would leak how many leading bytes matched through
// timing, defeating the point of hashing the password in the first
// place. Returns false (never throws) for an unrecognized algorithm, so
// a corrupted or foreign-format stored hash just fails to authenticate
// rather than crashing the caller.
bool verify_password(std::string_view password, const PasswordHash& stored);

// Canonical on-disk form: "<algorithm>:<iterations>:<salt_hex>:<hash_hex>"
// — same "plain, self-describing text" convention as tree.hpp/index.hpp's
// canonical encodings, not JSON (this is local storage, not the HTTP
// wire format — see core/remote_protocol.hpp for that boundary).
std::string encode_password_hash(const PasswordHash& hash);
std::optional<PasswordHash> decode_password_hash(std::string_view encoded);

} // namespace forge::domain

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace forge::domain {

enum class TokenKind { Session, PersonalAccessToken };

std::string_view token_kind_to_string(TokenKind kind);
std::optional<TokenKind> parse_token_kind(std::string_view text);

struct StoredToken {
    std::string id;             // random, not secret — the lookup key (see issue_token)
    std::string secret_hash_hex; // SHA-256 of the secret; the secret itself is never stored
    std::string username;
    TokenKind kind;
    std::int64_t created_at;
    std::optional<std::int64_t> expires_at; // nullopt: never expires

    friend bool operator==(const StoredToken&, const StoredToken&) = default;
};

// Generates a fresh high-entropy token (16 random bytes for the id, 32
// for the secret, both hex-encoded) and its StoredToken record. The
// bearer credential handed to the caller is "<id>.<secret>" — the id
// prefix lets a server look a presented token up in O(1) (by id) rather
// than hashing-and-comparing against every stored token, the same
// "opaque prefix, hashed remainder" shape many real token formats use.
// The plaintext secret exists only in the returned string: it can never
// be recovered from `StoredToken` afterward, the same one-way property
// PasswordHash has.
std::pair<std::string, StoredToken> issue_token(
    std::string_view username, TokenKind kind, std::optional<std::int64_t> ttl_seconds, std::int64_t now);

// Splits a presented "<id>.<secret>" bearer credential. Doesn't verify
// anything — the caller looks `id` up (see storage/auth_store.hpp) and
// passes the found StoredToken plus `secret` to verify_token().
std::optional<std::pair<std::string, std::string>> split_bearer_token(std::string_view presented);

// Constant-time verification (OpenSSL's CRYPTO_memcmp, same reasoning
// as domain/password.hpp's verify_password): hashes `secret` and
// compares against `stored.secret_hash_hex`, and checks `stored.expires_at`
// against `now`.
bool verify_token(std::string_view secret, const StoredToken& stored, std::int64_t now);

} // namespace forge::domain

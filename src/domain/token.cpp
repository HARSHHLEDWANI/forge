#include "domain/token.hpp"

#include <vector>

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include "core/error.hpp"
#include "core/hashing.hpp"

namespace forge::domain {

namespace {

constexpr std::size_t kIdBytes = 16;
constexpr std::size_t kSecretBytes = 32;

std::string to_hex(const unsigned char* bytes, std::size_t size) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(kDigits[(bytes[i] >> 4) & 0xF]);
        out.push_back(kDigits[bytes[i] & 0xF]);
    }
    return out;
}

std::string random_hex(std::size_t byte_count) {
    std::vector<unsigned char> bytes(byte_count);
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw core::ForgeError("failed to generate random token bytes");
    }
    return to_hex(bytes.data(), bytes.size());
}

std::string hash_secret_hex(std::string_view secret) {
    const core::Sha256Digest digest = core::sha256(secret);
    return to_hex(reinterpret_cast<const unsigned char*>(digest.data()), digest.size());
}

} // namespace

std::string_view token_kind_to_string(TokenKind kind) {
    switch (kind) {
        case TokenKind::Session: return "session";
        case TokenKind::PersonalAccessToken: return "pat";
    }
    return "unknown";
}

std::optional<TokenKind> parse_token_kind(std::string_view text) {
    if (text == "session") return TokenKind::Session;
    if (text == "pat") return TokenKind::PersonalAccessToken;
    return std::nullopt;
}

std::pair<std::string, StoredToken> issue_token(
    std::string_view username, TokenKind kind, std::optional<std::int64_t> ttl_seconds, std::int64_t now) {
    if (username.empty()) {
        throw core::ForgeError("cannot issue a token for an empty username");
    }

    StoredToken stored;
    stored.id = random_hex(kIdBytes);
    const std::string secret = random_hex(kSecretBytes);
    stored.secret_hash_hex = hash_secret_hex(secret);
    stored.username = std::string(username);
    stored.kind = kind;
    stored.created_at = now;
    stored.expires_at = ttl_seconds ? std::optional(now + *ttl_seconds) : std::nullopt;

    return {stored.id + "." + secret, stored};
}

std::optional<std::pair<std::string, std::string>> split_bearer_token(std::string_view presented) {
    const std::size_t dot = presented.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view id = presented.substr(0, dot);
    const std::string_view secret = presented.substr(dot + 1);
    if (id.empty() || secret.empty()) {
        return std::nullopt;
    }
    return std::make_pair(std::string(id), std::string(secret));
}

bool verify_token(std::string_view secret, const StoredToken& stored, std::int64_t now) {
    if (stored.expires_at && now >= *stored.expires_at) {
        return false;
    }
    const std::string candidate = hash_secret_hex(secret);
    return candidate.size() == stored.secret_hash_hex.size() &&
           CRYPTO_memcmp(candidate.data(), stored.secret_hash_hex.data(), candidate.size()) == 0;
}

} // namespace forge::domain

#include "domain/password.hpp"

#include <array>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "core/error.hpp"

namespace forge::domain {

namespace {

constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32; // SHA-256 output size

std::string to_hex(const std::vector<unsigned char>& bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char b : bytes) {
        out.push_back(kDigits[(b >> 4) & 0xF]);
        out.push_back(kDigits[b & 0xF]);
    }
    return out;
}

std::optional<std::vector<unsigned char>> from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<unsigned char> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int value = 0;
        for (int j = 0; j < 2; ++j) {
            const char c = hex[i + j];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= (c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= (c - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }
        out.push_back(static_cast<unsigned char>(value));
    }
    return out;
}

std::vector<unsigned char> derive(
    std::string_view password, const std::vector<unsigned char>& salt, unsigned int iterations) {
    std::vector<unsigned char> derived(kHashBytes);
    const int result = PKCS5_PBKDF2_HMAC(
        password.data(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()),
        static_cast<int>(iterations), EVP_sha256(), static_cast<int>(derived.size()), derived.data());
    if (result != 1) {
        throw core::ForgeError("password KDF failed");
    }
    return derived;
}

} // namespace

PasswordHash hash_password(std::string_view password) {
    if (password.empty()) {
        throw core::ForgeError("password must not be empty");
    }

    std::vector<unsigned char> salt(kSaltBytes);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw core::ForgeError("failed to generate a random salt");
    }

    PasswordHash result;
    result.salt_hex = to_hex(salt);
    result.hash_hex = to_hex(derive(password, salt, result.iterations));
    return result;
}

bool verify_password(std::string_view password, const PasswordHash& stored) {
    if (stored.algorithm != "pbkdf2-hmac-sha256") {
        return false;
    }
    const std::optional<std::vector<unsigned char>> salt = from_hex(stored.salt_hex);
    const std::optional<std::vector<unsigned char>> expected = from_hex(stored.hash_hex);
    if (!salt || !expected || expected->size() != kHashBytes) {
        return false;
    }

    const std::vector<unsigned char> candidate = derive(password, *salt, stored.iterations);
    return candidate.size() == expected->size() &&
           CRYPTO_memcmp(candidate.data(), expected->data(), candidate.size()) == 0;
}

std::string encode_password_hash(const PasswordHash& hash) {
    return hash.algorithm + ":" + std::to_string(hash.iterations) + ":" + hash.salt_hex + ":" + hash.hash_hex;
}

std::optional<PasswordHash> decode_password_hash(std::string_view encoded) {
    const std::size_t first = encoded.find(':');
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second = encoded.find(':', first + 1);
    if (second == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t third = encoded.find(':', second + 1);
    if (third == std::string_view::npos) {
        return std::nullopt;
    }

    PasswordHash result;
    result.algorithm = std::string(encoded.substr(0, first));
    try {
        result.iterations = static_cast<unsigned int>(std::stoul(std::string(encoded.substr(first + 1, second - first - 1))));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    result.salt_hex = std::string(encoded.substr(second + 1, third - second - 1));
    result.hash_hex = std::string(encoded.substr(third + 1));
    if (result.salt_hex.empty() || result.hash_hex.empty()) {
        return std::nullopt;
    }
    return result;
}

} // namespace forge::domain

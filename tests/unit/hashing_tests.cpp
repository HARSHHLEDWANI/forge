#include "core/hashing.hpp"
#include "support/test_framework.hpp"

using forge::core::Sha256Hasher;
using forge::core::sha256;

namespace {

std::string to_hex(const forge::core::Sha256Digest& digest) {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (std::byte b : digest) {
        const auto value = std::to_integer<unsigned int>(b);
        result.push_back(kHexDigits[(value >> 4) & 0xF]);
        result.push_back(kHexDigits[value & 0xF]);
    }
    return result;
}

} // namespace

// NIST SHA-256 test vectors.
FORGE_TEST_CASE(sha256_matches_known_vector_for_empty_input) {
    FORGE_CHECK(
        to_hex(sha256("")) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

FORGE_TEST_CASE(sha256_matches_known_vector_for_abc) {
    FORGE_CHECK(
        to_hex(sha256("abc")) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

FORGE_TEST_CASE(sha256_is_binary_safe) {
    const std::string data("a\0b", 3);
    const std::string other("a\0c", 3);
    FORGE_CHECK(sha256(data) != sha256(other));
}

FORGE_TEST_CASE(sha256_incremental_matches_one_shot) {
    Sha256Hasher hasher;
    hasher.update("ab");
    hasher.update("c");
    FORGE_CHECK(hasher.finish() == sha256("abc"));
}

#include "core/error.hpp"
#include "domain/password.hpp"
#include "support/test_framework.hpp"

using forge::domain::decode_password_hash;
using forge::domain::encode_password_hash;
using forge::domain::hash_password;
using forge::domain::PasswordHash;
using forge::domain::verify_password;

FORGE_TEST_CASE(hash_password_produces_a_verifiable_hash) {
    const PasswordHash hash = hash_password("correct horse battery staple");
    FORGE_CHECK(verify_password("correct horse battery staple", hash));
}

FORGE_TEST_CASE(verify_password_rejects_the_wrong_password) {
    const PasswordHash hash = hash_password("correct horse battery staple");
    FORGE_CHECK(!verify_password("wrong password", hash));
}

FORGE_TEST_CASE(hash_password_salts_so_identical_passwords_hash_differently) {
    const PasswordHash a = hash_password("same password");
    const PasswordHash b = hash_password("same password");
    FORGE_CHECK(a.salt_hex != b.salt_hex);
    FORGE_CHECK(a.hash_hex != b.hash_hex);
    FORGE_CHECK(verify_password("same password", a));
    FORGE_CHECK(verify_password("same password", b));
}

FORGE_TEST_CASE(hash_password_rejects_an_empty_password) {
    bool threw = false;
    try {
        hash_password("");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(verify_password_rejects_an_unrecognized_algorithm) {
    PasswordHash hash = hash_password("a password");
    hash.algorithm = "some-future-algorithm";
    FORGE_CHECK(!verify_password("a password", hash));
}

FORGE_TEST_CASE(password_hash_round_trips_through_encode_and_decode) {
    const PasswordHash original = hash_password("round trip me");
    const auto decoded = decode_password_hash(encode_password_hash(original));
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(*decoded == original);
    FORGE_CHECK(verify_password("round trip me", *decoded));
}

FORGE_TEST_CASE(decode_password_hash_rejects_malformed_input) {
    FORGE_CHECK(!decode_password_hash("not enough fields").has_value());
    FORGE_CHECK(!decode_password_hash("algo:notanumber:salt:hash").has_value());
    FORGE_CHECK(!decode_password_hash("algo:100::hash").has_value());
}

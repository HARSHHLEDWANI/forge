#include "core/base64.hpp"
#include "support/test_framework.hpp"

using forge::core::base64_decode;
using forge::core::base64_encode;
using forge::core::base64_encode_unpadded;

FORGE_TEST_CASE(base64_encode_matches_known_vectors) {
    FORGE_CHECK(base64_encode("") == "");
    FORGE_CHECK(base64_encode("f") == "Zg==");
    FORGE_CHECK(base64_encode("fo") == "Zm8=");
    FORGE_CHECK(base64_encode("foo") == "Zm9v");
    FORGE_CHECK(base64_encode("foob") == "Zm9vYg==");
    FORGE_CHECK(base64_encode("fooba") == "Zm9vYmE=");
    FORGE_CHECK(base64_encode("foobar") == "Zm9vYmFy");
}

FORGE_TEST_CASE(base64_encode_unpadded_omits_padding) {
    FORGE_CHECK(base64_encode_unpadded("f") == "Zg");
    FORGE_CHECK(base64_encode_unpadded("fo") == "Zm8");
    FORGE_CHECK(base64_encode_unpadded("foo") == "Zm9v");
}

FORGE_TEST_CASE(base64_decode_matches_known_vectors) {
    FORGE_CHECK(base64_decode("Zg==").value_or("x") == "f");
    FORGE_CHECK(base64_decode("Zm8=").value_or("x") == "fo");
    FORGE_CHECK(base64_decode("Zm9v").value_or("x") == "foo");
    FORGE_CHECK(base64_decode("Zm9vYg==").value_or("x") == "foob");
}

FORGE_TEST_CASE(base64_decode_accepts_unpadded_input) {
    FORGE_CHECK(base64_decode("Zg").value_or("x") == "f");
    FORGE_CHECK(base64_decode("Zm8").value_or("x") == "fo");
}

FORGE_TEST_CASE(base64_round_trips_binary_data) {
    const std::string binary("\x00\x01\xff\xfe\x7f", 5);
    FORGE_CHECK(base64_decode(base64_encode(binary)).value_or("x") == binary);
}

FORGE_TEST_CASE(base64_decode_rejects_invalid_characters) {
    FORGE_CHECK(!base64_decode("not valid base64!!").has_value());
}

FORGE_TEST_CASE(base64_decode_rejects_excess_padding) {
    FORGE_CHECK(!base64_decode("Zg===").has_value());
}

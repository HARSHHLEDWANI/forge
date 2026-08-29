#include <string>

#include "core/object_encoding.hpp"
#include "core/object_id.hpp"
#include "support/test_framework.hpp"

using forge::core::decode_canonical_object;
using forge::core::encode_canonical_object;
using forge::core::ObjectId;

FORGE_TEST_CASE(encode_canonical_object_matches_exact_expected_bytes) {
    const std::string encoded = encode_canonical_object("blob", "hi");
    const std::string expected = std::string("blob 2", 6) + '\0' + "hi";
    FORGE_CHECK(encoded == expected);
}

FORGE_TEST_CASE(encode_canonical_object_is_binary_safe_in_payload) {
    const std::string payload("a\0b", 3);
    const std::string encoded = encode_canonical_object("blob", payload);
    FORGE_CHECK(encoded.size() == std::string("blob 3").size() + 1 + payload.size());
    FORGE_CHECK(encoded.substr(encoded.size() - payload.size()) == payload);
}

FORGE_TEST_CASE(encode_canonical_object_different_type_changes_object_id) {
    // Same raw payload, different declared type, must never collide.
    const ObjectId as_blob = ObjectId::of(encode_canonical_object("blob", "same content"));
    const ObjectId as_tree = ObjectId::of(encode_canonical_object("tree", "same content"));
    FORGE_CHECK(as_blob != as_tree);
}

FORGE_TEST_CASE(decode_canonical_object_round_trips_encode) {
    const std::string encoded = encode_canonical_object("blob", "hello");
    const auto decoded = decode_canonical_object(encoded);
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->type == "blob");
    FORGE_CHECK(decoded->payload == "hello");
}

FORGE_TEST_CASE(decode_canonical_object_round_trips_binary_payload) {
    const std::string payload("a\0b\0c", 5);
    const std::string encoded = encode_canonical_object("blob", payload);
    const auto decoded = decode_canonical_object(encoded);
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->payload == payload);
}

FORGE_TEST_CASE(decode_canonical_object_rejects_missing_space) {
    FORGE_CHECK(!decode_canonical_object("blobnospace").has_value());
}

FORGE_TEST_CASE(decode_canonical_object_rejects_missing_nul) {
    FORGE_CHECK(!decode_canonical_object("blob 5nonul").has_value());
}

FORGE_TEST_CASE(decode_canonical_object_rejects_non_numeric_size) {
    const std::string malformed = std::string("blob abc") + '\0' + "xyz";
    FORGE_CHECK(!decode_canonical_object(malformed).has_value());
}

FORGE_TEST_CASE(decode_canonical_object_rejects_size_payload_mismatch) {
    const std::string malformed = std::string("blob 99") + '\0' + "short";
    FORGE_CHECK(!decode_canonical_object(malformed).has_value());
}

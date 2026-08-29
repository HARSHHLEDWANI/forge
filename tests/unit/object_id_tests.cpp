#include <cctype>
#include <string>
#include <unordered_map>

#include "core/object_id.hpp"
#include "support/test_framework.hpp"

using forge::core::ObjectId;

FORGE_TEST_CASE(object_id_of_matches_known_sha256_hex) {
    const ObjectId id = ObjectId::of("abc");
    FORGE_CHECK(id.to_hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

FORGE_TEST_CASE(object_id_parse_round_trips_to_hex) {
    const ObjectId original = ObjectId::of("hello");
    const auto parsed = ObjectId::parse(original.to_hex());
    FORGE_CHECK(parsed.has_value());
    FORGE_CHECK(*parsed == original);
}

FORGE_TEST_CASE(object_id_parse_rejects_wrong_length) {
    FORGE_CHECK(!ObjectId::parse("").has_value());
    FORGE_CHECK(!ObjectId::parse("ab").has_value());
    FORGE_CHECK(!ObjectId::parse(std::string(63, 'a')).has_value());
    FORGE_CHECK(!ObjectId::parse(std::string(65, 'a')).has_value());
}

FORGE_TEST_CASE(object_id_parse_rejects_non_hex_characters) {
    // 64 characters, but 'g' is not a valid hex digit.
    const std::string invalid = "g" + std::string(63, '0');
    FORGE_CHECK(!ObjectId::parse(invalid).has_value());
}

FORGE_TEST_CASE(object_id_parse_accepts_uppercase_hex) {
    const ObjectId original = ObjectId::of("hello");
    std::string upper = original.to_hex();
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const auto parsed = ObjectId::parse(upper);
    FORGE_CHECK(parsed.has_value());
    FORGE_CHECK(*parsed == original);
}

FORGE_TEST_CASE(object_id_different_content_yields_different_id) {
    FORGE_CHECK(ObjectId::of("a") != ObjectId::of("b"));
}

FORGE_TEST_CASE(object_id_usable_as_hash_map_key) {
    std::unordered_map<ObjectId, int> map;
    const ObjectId key = ObjectId::of("mapped");
    map[key] = 42;
    FORGE_CHECK(map.at(ObjectId::of("mapped")) == 42);
}

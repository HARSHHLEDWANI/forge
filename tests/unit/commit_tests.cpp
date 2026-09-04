#include "core/commit.hpp"
#include "support/test_framework.hpp"

using forge::core::Commit;
using forge::core::decode_commit;
using forge::core::encode_commit;
using forge::core::ObjectId;

FORGE_TEST_CASE(decode_commit_round_trips_encode_with_no_parents) {
    const Commit original{ObjectId::of("tree"), {}, "A Author <a@example.com>", 1700000000, "initial commit"};

    const auto decoded = decode_commit(encode_commit(original));
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(*decoded == original);
}

FORGE_TEST_CASE(decode_commit_round_trips_encode_with_parents_in_order) {
    const Commit original{
        ObjectId::of("tree"), {ObjectId::of("p1"), ObjectId::of("p2")}, "A Author <a@example.com>", 1700000001,
        "second commit"};

    const auto decoded = decode_commit(encode_commit(original));
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->parent_ids == original.parent_ids);
}

FORGE_TEST_CASE(decode_commit_preserves_multiline_message) {
    const Commit original{ObjectId::of("tree"), {}, "A Author <a@example.com>", 1700000002, "line one\nline two\n"};

    const auto decoded = decode_commit(encode_commit(original));
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->message == "line one\nline two\n");
}

FORGE_TEST_CASE(decode_commit_accepts_empty_message) {
    const Commit original{ObjectId::of("tree"), {}, "A Author <a@example.com>", 1700000003, ""};

    const auto decoded = decode_commit(encode_commit(original));
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->message.empty());
}

FORGE_TEST_CASE(decode_commit_rejects_missing_tree_line) {
    FORGE_CHECK(!decode_commit("author A <a@example.com>\ntimestamp 1\n\nmsg").has_value());
}

FORGE_TEST_CASE(decode_commit_rejects_bad_tree_id) {
    FORGE_CHECK(!decode_commit("tree not-a-valid-hex\nauthor A <a@example.com>\ntimestamp 1\n\nmsg").has_value());
}

FORGE_TEST_CASE(decode_commit_rejects_bad_parent_id) {
    const std::string malformed =
        "tree " + ObjectId::of("t").to_hex() + "\nparent not-hex\nauthor A <a@example.com>\ntimestamp 1\n\nmsg";
    FORGE_CHECK(!decode_commit(malformed).has_value());
}

FORGE_TEST_CASE(decode_commit_rejects_missing_author_line) {
    const std::string malformed = "tree " + ObjectId::of("t").to_hex() + "\ntimestamp 1\n\nmsg";
    FORGE_CHECK(!decode_commit(malformed).has_value());
}

FORGE_TEST_CASE(decode_commit_rejects_non_numeric_timestamp) {
    const std::string malformed =
        "tree " + ObjectId::of("t").to_hex() + "\nauthor A <a@example.com>\ntimestamp not-a-number\n\nmsg";
    FORGE_CHECK(!decode_commit(malformed).has_value());
}

FORGE_TEST_CASE(decode_commit_rejects_missing_blank_line_separator) {
    const std::string malformed = "tree " + ObjectId::of("t").to_hex() + "\nauthor A <a@example.com>\ntimestamp 1\nmsg";
    FORGE_CHECK(!decode_commit(malformed).has_value());
}

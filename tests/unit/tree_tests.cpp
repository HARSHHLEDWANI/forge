#include "core/error.hpp"
#include "core/tree.hpp"
#include "support/test_framework.hpp"

using forge::core::decode_tree;
using forge::core::EntryMode;
using forge::core::ObjectId;
using forge::core::parse_mode_string;
using forge::core::to_mode_string;
using forge::core::Tree;
using forge::core::TreeEntry;

FORGE_TEST_CASE(mode_strings_round_trip) {
    for (EntryMode mode :
         {EntryMode::RegularFile, EntryMode::ExecutableFile, EntryMode::Directory, EntryMode::Symlink}) {
        const auto parsed = parse_mode_string(to_mode_string(mode));
        FORGE_CHECK(parsed.has_value());
        FORGE_CHECK(*parsed == mode);
    }
}

FORGE_TEST_CASE(parse_mode_string_rejects_garbage) {
    FORGE_CHECK(!parse_mode_string("999999").has_value());
    FORGE_CHECK(!parse_mode_string("").has_value());
}

FORGE_TEST_CASE(tree_encode_sorts_entries_by_name_regardless_of_input_order) {
    const ObjectId id = ObjectId::of("x");
    const Tree tree(
        {TreeEntry{"zebra", EntryMode::RegularFile, id}, TreeEntry{"apple", EntryMode::RegularFile, id},
         TreeEntry{"mango", EntryMode::RegularFile, id}});

    const std::string encoded = tree.encode();
    const std::size_t apple_pos = encoded.find("apple");
    const std::size_t mango_pos = encoded.find("mango");
    const std::size_t zebra_pos = encoded.find("zebra");
    FORGE_CHECK(apple_pos < mango_pos);
    FORGE_CHECK(mango_pos < zebra_pos);
}

FORGE_TEST_CASE(tree_encode_is_deterministic_regardless_of_construction_order) {
    const ObjectId id_a = ObjectId::of("a");
    const ObjectId id_b = ObjectId::of("b");

    const Tree first({TreeEntry{"a.txt", EntryMode::RegularFile, id_a}, TreeEntry{"b.txt", EntryMode::RegularFile, id_b}});
    const Tree second({TreeEntry{"b.txt", EntryMode::RegularFile, id_b}, TreeEntry{"a.txt", EntryMode::RegularFile, id_a}});

    FORGE_CHECK(first.encode() == second.encode());
}

FORGE_TEST_CASE(tree_constructor_throws_on_duplicate_names) {
    const ObjectId id = ObjectId::of("x");
    bool threw = false;
    try {
        const Tree tree({TreeEntry{"dup", EntryMode::RegularFile, id}, TreeEntry{"dup", EntryMode::Directory, id}});
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(decode_tree_round_trips_encode) {
    const ObjectId id_a = ObjectId::of("a");
    const ObjectId id_b = ObjectId::of("b");
    const Tree original(
        {TreeEntry{"a.txt", EntryMode::RegularFile, id_a}, TreeEntry{"sub", EntryMode::Directory, id_b}});

    const auto decoded = decode_tree(original.encode());
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->entries() == original.entries());
}

FORGE_TEST_CASE(decode_tree_rejects_missing_trailing_newline) {
    FORGE_CHECK(!decode_tree("100644 " + ObjectId::of("x").to_hex() + " file.txt").has_value());
}

FORGE_TEST_CASE(decode_tree_rejects_bad_mode) {
    const std::string line = "999999 " + ObjectId::of("x").to_hex() + " file.txt\n";
    FORGE_CHECK(!decode_tree(line).has_value());
}

FORGE_TEST_CASE(decode_tree_rejects_bad_object_id) {
    FORGE_CHECK(!decode_tree("100644 not-a-valid-hex-id file.txt\n").has_value());
}

FORGE_TEST_CASE(decode_tree_rejects_duplicate_names) {
    const std::string hex = ObjectId::of("x").to_hex();
    const std::string malformed = "100644 " + hex + " dup\n" + "040000 " + hex + " dup\n";
    FORGE_CHECK(!decode_tree(malformed).has_value());
}

FORGE_TEST_CASE(decode_tree_accepts_names_with_spaces) {
    const ObjectId id = ObjectId::of("x");
    const Tree original({TreeEntry{"file with spaces.txt", EntryMode::RegularFile, id}});
    const auto decoded = decode_tree(original.encode());
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->entries().at(0).name == "file with spaces.txt");
}

FORGE_TEST_CASE(empty_tree_encodes_to_empty_payload) {
    const Tree tree({});
    FORGE_CHECK(tree.encode().empty());
}

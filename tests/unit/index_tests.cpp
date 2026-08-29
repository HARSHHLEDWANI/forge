#include "core/error.hpp"
#include "core/index.hpp"
#include "support/test_framework.hpp"

using forge::core::decode_index;
using forge::core::EntryMode;
using forge::core::Index;
using forge::core::IndexEntry;
using forge::core::ObjectId;

FORGE_TEST_CASE(index_starts_empty) {
    const Index index;
    FORGE_CHECK(index.entries().empty());
}

FORGE_TEST_CASE(upsert_adds_new_entry) {
    Index index;
    const ObjectId id = ObjectId::of("x");
    index.upsert(IndexEntry{"a.txt", EntryMode::RegularFile, id});
    FORGE_CHECK(index.entries().size() == 1);
    FORGE_CHECK(index.find("a.txt").has_value());
}

FORGE_TEST_CASE(upsert_replaces_existing_entry_for_same_path) {
    Index index;
    const ObjectId first = ObjectId::of("first");
    const ObjectId second = ObjectId::of("second");
    index.upsert(IndexEntry{"a.txt", EntryMode::RegularFile, first});
    index.upsert(IndexEntry{"a.txt", EntryMode::ExecutableFile, second});

    FORGE_CHECK(index.entries().size() == 1);
    const auto entry = index.find("a.txt");
    FORGE_CHECK(entry->blob_id == second);
    FORGE_CHECK(entry->mode == EntryMode::ExecutableFile);
}

FORGE_TEST_CASE(remove_deletes_existing_entry_and_reports_true) {
    Index index;
    index.upsert(IndexEntry{"a.txt", EntryMode::RegularFile, ObjectId::of("x")});
    FORGE_CHECK(index.remove("a.txt"));
    FORGE_CHECK(!index.find("a.txt").has_value());
}

FORGE_TEST_CASE(remove_returns_false_for_untracked_path) {
    Index index;
    FORGE_CHECK(!index.remove("missing.txt"));
}

FORGE_TEST_CASE(encode_sorts_entries_by_path) {
    Index index;
    const ObjectId id = ObjectId::of("x");
    index.upsert(IndexEntry{"z.txt", EntryMode::RegularFile, id});
    index.upsert(IndexEntry{"a.txt", EntryMode::RegularFile, id});

    const std::string encoded = index.encode();
    FORGE_CHECK(encoded.find("a.txt") < encoded.find("z.txt"));
}

FORGE_TEST_CASE(encode_preserves_paths_with_slashes) {
    Index index;
    index.upsert(IndexEntry{"src/core/main.cpp", EntryMode::RegularFile, ObjectId::of("x")});
    const auto decoded = decode_index(index.encode());
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->entries().at(0).path == "src/core/main.cpp");
}

FORGE_TEST_CASE(index_constructor_throws_on_duplicate_path) {
    const ObjectId id = ObjectId::of("x");
    bool threw = false;
    try {
        const Index index({IndexEntry{"dup", EntryMode::RegularFile, id}, IndexEntry{"dup", EntryMode::ExecutableFile, id}});
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(decode_index_rejects_duplicate_paths) {
    const std::string hex = ObjectId::of("x").to_hex();
    const std::string malformed = "100644 " + hex + " dup\n" + "100755 " + hex + " dup\n";
    FORGE_CHECK(!decode_index(malformed).has_value());
}

FORGE_TEST_CASE(decode_index_rejects_directory_mode) {
    const std::string line = "040000 " + ObjectId::of("x").to_hex() + " some/path\n";
    FORGE_CHECK(!decode_index(line).has_value());
}

FORGE_TEST_CASE(decode_index_round_trips_encode) {
    // Inserted out of alphabetical order on purpose: encode() sorts by
    // path, so the decoded order should match sorted, not insertion,
    // order.
    Index index;
    const IndexEntry entry_b{"b/c.sh", EntryMode::ExecutableFile, ObjectId::of("b")};
    const IndexEntry entry_a{"a.txt", EntryMode::RegularFile, ObjectId::of("a")};
    index.upsert(entry_b);
    index.upsert(entry_a);

    const auto decoded = decode_index(index.encode());
    FORGE_CHECK(decoded.has_value());
    const std::vector<IndexEntry> expected_sorted{entry_a, entry_b};
    FORGE_CHECK(decoded->entries() == expected_sorted);
}

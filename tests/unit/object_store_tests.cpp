#include <fstream>

#include "core/blob.hpp"
#include "core/error.hpp"
#include "core/object_id.hpp"
#include "storage/object_store.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::Blob;
using forge::storage::ObjectStore;
using forge::test::TempDir;

FORGE_TEST_CASE(put_then_get_round_trips_type_and_payload) {
    TempDir dir;
    ObjectStore store(dir.path());

    const auto id = store.put("blob", "hello world");
    const auto stored = store.get(id);

    FORGE_CHECK(stored.type == "blob");
    FORGE_CHECK(stored.payload == "hello world");
}

FORGE_TEST_CASE(put_lays_out_object_at_hash_prefix_path) {
    TempDir dir;
    ObjectStore store(dir.path());

    const auto id = store.put("blob", "hello world");
    const std::string hex = id.to_hex();
    const std::filesystem::path expected = dir.path() / hex.substr(0, 2) / hex.substr(2);
    FORGE_CHECK(std::filesystem::exists(expected));
}

FORGE_TEST_CASE(identical_content_deduplicates_to_one_file) {
    TempDir dir;
    ObjectStore store(dir.path());

    const auto first = store.put("blob", "same content");
    const auto second = store.put("blob", "same content");
    FORGE_CHECK(first == second);

    int file_count = 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator(dir.path())) {
        if (entry.is_regular_file()) {
            ++file_count;
        }
    }
    FORGE_CHECK(file_count == 1);
}

FORGE_TEST_CASE(different_type_same_payload_yields_different_objects) {
    TempDir dir;
    ObjectStore store(dir.path());

    const auto as_blob = store.put("blob", "x");
    const auto as_tree = store.put("tree", "x");
    FORGE_CHECK(as_blob != as_tree);
}

FORGE_TEST_CASE(contains_reflects_store_state) {
    TempDir dir;
    ObjectStore store(dir.path());

    const auto id = store.put("blob", "present");
    FORGE_CHECK(store.contains(id));

    const auto missing_id = forge::core::ObjectId::of("never stored");
    FORGE_CHECK(!store.contains(missing_id));
}

FORGE_TEST_CASE(get_throws_for_missing_object) {
    TempDir dir;
    ObjectStore store(dir.path());
    const auto id = forge::core::ObjectId::of("never stored");

    bool threw = false;
    try {
        store.get(id);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(get_detects_corrupted_object) {
    TempDir dir;
    ObjectStore store(dir.path());
    const auto id = store.put("blob", "original content");

    const std::string hex = id.to_hex();
    const std::filesystem::path path = dir.path() / hex.substr(0, 2) / hex.substr(2);
    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "this is not the object content that was originally written";
    }

    bool threw = false;
    try {
        store.get(id);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(put_blob_and_get_blob_round_trip) {
    TempDir dir;
    ObjectStore store(dir.path());

    const auto id = store.put_blob(Blob{"blob content"});
    const Blob result = store.get_blob(id);
    FORGE_CHECK(result.content == "blob content");
}

FORGE_TEST_CASE(get_blob_rejects_non_blob_object) {
    TempDir dir;
    ObjectStore store(dir.path());
    const auto tree_id = store.put("tree", "not a blob");

    bool threw = false;
    try {
        store.get_blob(tree_id);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(store_round_trips_binary_payload) {
    TempDir dir;
    ObjectStore store(dir.path());
    const std::string payload("a\0b\0c", 5);

    const auto id = store.put("blob", payload);
    FORGE_CHECK(store.get(id).payload == payload);
}

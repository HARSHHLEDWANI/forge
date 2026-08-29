#include <fstream>

#include "core/error.hpp"
#include "storage/index_store.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::EntryMode;
using forge::core::Index;
using forge::core::IndexEntry;
using forge::core::ObjectId;
using forge::storage::IndexStore;
using forge::test::TempDir;

FORGE_TEST_CASE(load_on_missing_file_returns_empty_index) {
    TempDir dir;
    IndexStore store(dir.path() / "index");
    FORGE_CHECK(store.load().entries().empty());
}

FORGE_TEST_CASE(save_then_load_round_trips) {
    TempDir dir;
    IndexStore store(dir.path() / "index");

    Index index;
    index.upsert(IndexEntry{"a.txt", EntryMode::RegularFile, ObjectId::of("a")});
    store.save(index);

    const Index loaded = store.load();
    FORGE_CHECK(loaded.entries() == index.entries());
}

FORGE_TEST_CASE(save_leaves_no_temp_file_behind) {
    TempDir dir;
    IndexStore store(dir.path() / "index");
    store.save(Index{});

    int entry_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
        (void)entry;
        ++entry_count;
    }
    FORGE_CHECK(entry_count == 1); // only "index", no ".tmp-*" leftovers
}

FORGE_TEST_CASE(load_throws_on_corrupted_index_file) {
    TempDir dir;
    const std::filesystem::path index_path = dir.path() / "index";
    {
        std::ofstream out(index_path, std::ios::binary);
        out << "this is not a valid index line";
    }

    IndexStore store(index_path);
    bool threw = false;
    try {
        store.load();
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

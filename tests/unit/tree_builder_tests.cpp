#include <fstream>

#include "core/tree_builder.hpp"
#include "storage/object_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::build_tree_from_directory;
using forge::core::EntryMode;
using forge::storage::ObjectStore;
using forge::test::TempDir;

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

} // namespace

FORGE_TEST_CASE(build_tree_from_directory_stores_single_file) {
    TempDir dir;
    write_file(dir.path() / "hello.txt", "hello content");

    TempDir objects_dir;
    ObjectStore store(objects_dir.path());
    const auto root_id = build_tree_from_directory(store, dir.path());

    const auto tree = store.get_tree(root_id);
    FORGE_CHECK(tree.entries().size() == 1);
    FORGE_CHECK(tree.entries().at(0).name == "hello.txt");
    FORGE_CHECK(tree.entries().at(0).mode == EntryMode::RegularFile);
    FORGE_CHECK(store.get_blob(tree.entries().at(0).id).content == "hello content");
}

FORGE_TEST_CASE(build_tree_from_directory_recurses_into_subdirectories) {
    TempDir dir;
    std::filesystem::create_directories(dir.path() / "sub");
    write_file(dir.path() / "sub" / "nested.txt", "nested content");

    TempDir objects_dir;
    ObjectStore store(objects_dir.path());
    const auto root_id = build_tree_from_directory(store, dir.path());

    const auto root = store.get_tree(root_id);
    FORGE_CHECK(root.entries().size() == 1);
    FORGE_CHECK(root.entries().at(0).name == "sub");
    FORGE_CHECK(root.entries().at(0).mode == EntryMode::Directory);

    const auto subtree = store.get_tree(root.entries().at(0).id);
    FORGE_CHECK(subtree.entries().size() == 1);
    FORGE_CHECK(subtree.entries().at(0).name == "nested.txt");
    FORGE_CHECK(store.get_blob(subtree.entries().at(0).id).content == "nested content");
}

FORGE_TEST_CASE(build_tree_from_directory_skips_forge_metadata_dir) {
    TempDir dir;
    forge::storage::initialize_repository(dir.path());
    write_file(dir.path() / "tracked.txt", "tracked");

    TempDir objects_dir;
    ObjectStore store(objects_dir.path());
    const auto root_id = build_tree_from_directory(store, dir.path());

    const auto root = store.get_tree(root_id);
    FORGE_CHECK(root.entries().size() == 1);
    FORGE_CHECK(root.entries().at(0).name == "tracked.txt");
}

FORGE_TEST_CASE(build_tree_from_directory_handles_symlink_without_following_it) {
    TempDir dir;
    write_file(dir.path() / "target.txt", "target content");
    std::filesystem::create_symlink(dir.path() / "target.txt", dir.path() / "link.txt");

    TempDir objects_dir;
    ObjectStore store(objects_dir.path());
    const auto root_id = build_tree_from_directory(store, dir.path());

    const auto root = store.get_tree(root_id);
    FORGE_CHECK(root.entries().size() == 2);

    bool found_symlink = false;
    for (const auto& entry : root.entries()) {
        if (entry.name == "link.txt") {
            found_symlink = true;
            FORGE_CHECK(entry.mode == EntryMode::Symlink);
            const auto link_blob = store.get_blob(entry.id);
            FORGE_CHECK(link_blob.content == (dir.path() / "target.txt").generic_string());
        }
    }
    FORGE_CHECK(found_symlink);
}

FORGE_TEST_CASE(build_tree_from_directory_on_empty_directory_yields_empty_tree) {
    TempDir dir;
    TempDir objects_dir;
    ObjectStore store(objects_dir.path());
    const auto root_id = build_tree_from_directory(store, dir.path());

    const auto tree = store.get_tree(root_id);
    FORGE_CHECK(tree.entries().empty());
}

FORGE_TEST_CASE(build_tree_from_directory_is_deterministic_across_runs) {
    TempDir dir;
    write_file(dir.path() / "a.txt", "a");
    write_file(dir.path() / "b.txt", "b");
    std::filesystem::create_directories(dir.path() / "sub");
    write_file(dir.path() / "sub" / "c.txt", "c");

    TempDir objects_dir;
    ObjectStore store(objects_dir.path());
    const auto first_id = build_tree_from_directory(store, dir.path());
    const auto second_id = build_tree_from_directory(store, dir.path());

    FORGE_CHECK(first_id == second_id);
}

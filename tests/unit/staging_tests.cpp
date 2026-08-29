#include <fstream>

#include "core/blob.hpp"
#include "core/error.hpp"
#include "core/staging.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::AddResult;
using forge::core::IgnoreRules;
using forge::core::stage_path;
using forge::storage::IndexStore;
using forge::storage::ObjectStore;
using forge::test::TempDir;

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

struct StagingFixture {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects{objects_dir.path()};
    IndexStore index_store{repo.path() / "index"};
    IgnoreRules ignore_rules = IgnoreRules::parse("");
};

} // namespace

FORGE_TEST_CASE(stage_single_file) {
    StagingFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "hello");

    const AddResult result =
        stage_path(fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(),
                   fixture.repo.path() / "a.txt");

    FORGE_CHECK(result.staged.size() == 1);
    FORGE_CHECK(result.staged.at(0) == "a.txt");
    const auto index = fixture.index_store.load();
    FORGE_CHECK(index.find("a.txt")->blob_id == fixture.objects.put_blob(forge::core::Blob{"hello"}));
}

FORGE_TEST_CASE(stage_directory_recursively) {
    StagingFixture fixture;
    std::filesystem::create_directories(fixture.repo.path() / "sub");
    write_file(fixture.repo.path() / "a.txt", "a");
    write_file(fixture.repo.path() / "sub" / "b.txt", "b");

    const AddResult result = stage_path(
        fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(), fixture.repo.path());

    FORGE_CHECK(result.staged.size() == 2);
    const auto index = fixture.index_store.load();
    FORGE_CHECK(index.find("a.txt").has_value());
    FORGE_CHECK(index.find("sub/b.txt").has_value());
}

FORGE_TEST_CASE(stage_excludes_forge_metadata_dir) {
    StagingFixture fixture;
    forge::storage::initialize_repository(fixture.repo.path());
    write_file(fixture.repo.path() / "tracked.txt", "content");

    const AddResult result = stage_path(
        fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(), fixture.repo.path());

    FORGE_CHECK(result.staged.size() == 1);
    FORGE_CHECK(result.staged.at(0) == "tracked.txt");
}

FORGE_TEST_CASE(restaging_modified_file_updates_blob_id) {
    StagingFixture fixture;
    const std::filesystem::path file = fixture.repo.path() / "a.txt";
    write_file(file, "version 1");
    stage_path(fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(), file);
    const auto id_v1 = fixture.index_store.load().find("a.txt")->blob_id;

    write_file(file, "version 2");
    stage_path(fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(), file);
    const auto id_v2 = fixture.index_store.load().find("a.txt")->blob_id;

    FORGE_CHECK(id_v1 != id_v2);
}

FORGE_TEST_CASE(staging_deleted_tracked_file_removes_it_from_index) {
    StagingFixture fixture;
    const std::filesystem::path file = fixture.repo.path() / "a.txt";
    write_file(file, "content");
    stage_path(fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(), file);
    FORGE_CHECK(fixture.index_store.load().find("a.txt").has_value());

    std::filesystem::remove(file);
    const AddResult result =
        stage_path(fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(), file);

    FORGE_CHECK(result.removed.size() == 1);
    FORGE_CHECK(result.removed.at(0) == "a.txt");
    FORGE_CHECK(!fixture.index_store.load().find("a.txt").has_value());
}

FORGE_TEST_CASE(directory_add_stages_deletions_scoped_to_that_directory) {
    StagingFixture fixture;
    std::filesystem::create_directories(fixture.repo.path() / "sub");
    write_file(fixture.repo.path() / "sub" / "a.txt", "a");
    write_file(fixture.repo.path() / "sub" / "b.txt", "b");
    write_file(fixture.repo.path() / "outside.txt", "outside");
    stage_path(fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(), fixture.repo.path());

    std::filesystem::remove(fixture.repo.path() / "sub" / "a.txt");
    const AddResult result = stage_path(
        fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(),
        fixture.repo.path() / "sub");

    FORGE_CHECK(result.removed.size() == 1);
    FORGE_CHECK(result.removed.at(0) == "sub/a.txt");
    // Untouched files outside the "sub" pathspec must survive.
    FORGE_CHECK(fixture.index_store.load().find("outside.txt").has_value());
}

FORGE_TEST_CASE(forgeignore_rules_are_respected) {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects(objects_dir.path());
    IndexStore index_store(repo.path() / "index");
    const IgnoreRules ignore_rules = IgnoreRules::parse("*.log\n");

    write_file(repo.path() / "keep.txt", "keep");
    write_file(repo.path() / "skip.log", "skip");

    const AddResult result = stage_path(objects, index_store, ignore_rules, repo.path(), repo.path());

    FORGE_CHECK(result.staged.size() == 1);
    FORGE_CHECK(result.staged.at(0) == "keep.txt");
}

FORGE_TEST_CASE(staging_outside_repo_root_throws) {
    StagingFixture fixture;
    TempDir outside;
    write_file(outside.path() / "evil.txt", "evil");

    bool threw = false;
    try {
        stage_path(
            fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(),
            outside.path() / "evil.txt");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(staging_nonexistent_untracked_path_throws) {
    StagingFixture fixture;
    bool threw = false;
    try {
        stage_path(
            fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(),
            fixture.repo.path() / "never-existed.txt");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(restaging_unchanged_content_is_idempotent) {
    StagingFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "stable");
    stage_path(fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(), fixture.repo.path());
    const auto id_first = fixture.index_store.load().find("a.txt")->blob_id;

    stage_path(fixture.objects, fixture.index_store, fixture.ignore_rules, fixture.repo.path(), fixture.repo.path());
    const auto id_second = fixture.index_store.load().find("a.txt")->blob_id;

    FORGE_CHECK(id_first == id_second);
}

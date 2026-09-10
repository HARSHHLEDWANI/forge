#include <fstream>
#include <sstream>

#include "core/backup.hpp"
#include "core/branching.hpp"
#include "core/checkout.hpp"
#include "core/committing.hpp"
#include "core/error.hpp"
#include "core/ignore_rules.hpp"
#include "core/staging.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::BackupResult;
using forge::core::create_backup;
using forge::core::create_branch;
using forge::core::create_commit;
using forge::core::IgnoreRules;
using forge::core::ObjectId;
using forge::core::restore_backup;
using forge::core::stage_path;
using forge::core::verify_backup;
using forge::storage::IndexStore;
using forge::storage::ObjectStore;
using forge::storage::RefStore;
using forge::test::TempDir;

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

struct SourceRepoFixture {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects{objects_dir.path()};
    IndexStore index_store{repo.path() / ".forge" / "index"};
    RefStore refs{repo.path() / ".forge"};
    IgnoreRules ignore_rules = IgnoreRules::parse("");

    SourceRepoFixture() { forge::storage::initialize_repository(repo.path()); }

    void stage_everything() { stage_path(objects, index_store, ignore_rules, repo.path(), repo.path()); }

    ObjectId commit(const std::string& message) {
        return create_commit(objects, index_store, refs, "Test <t@example.com>", message, 1000).commit_id;
    }
};

} // namespace

FORGE_TEST_CASE(create_backup_then_verify_backup_reports_healthy) {
    SourceRepoFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "hello");
    fixture.stage_everything();
    fixture.commit("first");

    TempDir backup_dir;
    const BackupResult result = create_backup(fixture.objects, fixture.refs, backup_dir.path());
    FORGE_CHECK(result.objects_copied == 3); // blob, tree, commit
    FORGE_CHECK(result.objects_already_present == 0);

    FORGE_CHECK(verify_backup(backup_dir.path()).ok());
}

FORGE_TEST_CASE(create_backup_is_incremental_on_a_second_run) {
    SourceRepoFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "hello");
    fixture.stage_everything();
    fixture.commit("first");

    TempDir backup_dir;
    create_backup(fixture.objects, fixture.refs, backup_dir.path());

    write_file(fixture.repo.path() / "b.txt", "world");
    fixture.stage_everything();
    fixture.commit("second");

    const BackupResult second_run = create_backup(fixture.objects, fixture.refs, backup_dir.path());
    // Only the new blob/tree/commit are new; the old tree and blob for
    // a.txt are already in the backup and must not be re-copied.
    FORGE_CHECK(second_run.objects_copied == 3);
    FORGE_CHECK(second_run.objects_already_present > 0);
}

FORGE_TEST_CASE(restore_backup_reconstructs_working_tree_and_history) {
    SourceRepoFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "hello");
    fixture.stage_everything();
    fixture.commit("first");
    write_file(fixture.repo.path() / "b.txt", "world");
    fixture.stage_everything();
    const ObjectId second_commit = fixture.commit("second");

    TempDir backup_dir;
    create_backup(fixture.objects, fixture.refs, backup_dir.path());

    TempDir restore_target;
    const std::filesystem::path target_dir = restore_target.path() / "restored";
    restore_backup(backup_dir.path(), target_dir);

    FORGE_CHECK(read_file(target_dir / "a.txt") == "hello");
    FORGE_CHECK(read_file(target_dir / "b.txt") == "world");

    RefStore restored_refs(target_dir / ".forge");
    FORGE_CHECK(restored_refs.read_branch("main") == second_commit);
    FORGE_CHECK(restored_refs.read_head().branch.value_or("") == "main");
}

FORGE_TEST_CASE(restore_backup_preserves_every_branch_not_just_head) {
    SourceRepoFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "hello");
    fixture.stage_everything();
    fixture.commit("first");
    create_branch(fixture.refs, "feature");

    TempDir backup_dir;
    create_backup(fixture.objects, fixture.refs, backup_dir.path());

    TempDir restore_target;
    const std::filesystem::path target_dir = restore_target.path() / "restored";
    restore_backup(backup_dir.path(), target_dir);

    RefStore restored_refs(target_dir / ".forge");
    FORGE_CHECK(restored_refs.branch_exists("main"));
    FORGE_CHECK(restored_refs.branch_exists("feature"));
    FORGE_CHECK(restored_refs.read_branch("main") == restored_refs.read_branch("feature"));
}

FORGE_TEST_CASE(restore_backup_recovers_a_repository_after_the_source_is_destroyed) {
    // The actual disaster-recovery scenario the phase's "restore tests"
    // deliverable is about: the source is gone entirely, and the only
    // thing left is the backup.
    SourceRepoFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "important data");
    fixture.stage_everything();
    fixture.commit("first");

    TempDir backup_dir;
    create_backup(fixture.objects, fixture.refs, backup_dir.path());

    std::filesystem::remove_all(fixture.repo.path());
    std::filesystem::remove_all(fixture.objects_dir.path());

    TempDir restore_target;
    const std::filesystem::path target_dir = restore_target.path() / "recovered";
    restore_backup(backup_dir.path(), target_dir);
    FORGE_CHECK(read_file(target_dir / "a.txt") == "important data");
}

FORGE_TEST_CASE(verify_backup_detects_a_corrupted_backup_object) {
    SourceRepoFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "hello");
    fixture.stage_everything();
    const ObjectId commit_id = fixture.commit("first");

    TempDir backup_dir;
    create_backup(fixture.objects, fixture.refs, backup_dir.path());

    const std::string hex = commit_id.to_hex();
    const std::filesystem::path object_path = backup_dir.path() / "objects" / hex.substr(0, 2) / hex.substr(2);
    write_file(object_path, "corrupted bytes");

    FORGE_CHECK(!verify_backup(backup_dir.path()).ok());
}

FORGE_TEST_CASE(restore_backup_throws_for_a_directory_that_is_not_a_backup) {
    TempDir not_a_backup;
    TempDir restore_target;
    bool threw = false;
    try {
        restore_backup(not_a_backup.path(), restore_target.path() / "restored");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

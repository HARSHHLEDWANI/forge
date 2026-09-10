#include <fstream>

#include "core/branching.hpp"
#include "core/checkout.hpp"
#include "core/committing.hpp"
#include "core/ignore_rules.hpp"
#include "core/staging.hpp"
#include "core/verify.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::create_commit;
using forge::core::IgnoreRules;
using forge::core::ObjectId;
using forge::core::stage_path;
using forge::core::verify_repository;
using forge::core::VerifyReport;
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

struct VerifyFixture {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects{objects_dir.path()};
    IndexStore index_store{repo.path() / ".forge" / "index"};
    RefStore refs{repo.path() / ".forge"};
    IgnoreRules ignore_rules = IgnoreRules::parse("");

    VerifyFixture() { forge::storage::initialize_repository(repo.path()); }

    void stage_everything() { stage_path(objects, index_store, ignore_rules, repo.path(), repo.path()); }

    ObjectId commit(const std::string& message) {
        return create_commit(objects, index_store, refs, "Test <t@example.com>", message, 1000).commit_id;
    }
};

} // namespace

FORGE_TEST_CASE(verify_reports_healthy_on_a_clean_repository) {
    VerifyFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "content");
    fixture.stage_everything();
    fixture.commit("first");

    const VerifyReport report = verify_repository(fixture.objects, fixture.refs);
    FORGE_CHECK(report.ok());
    FORGE_CHECK(report.objects_checked > 0);
    FORGE_CHECK(report.orphaned_files.empty());
}

FORGE_TEST_CASE(verify_reports_healthy_on_a_fresh_repository_with_no_commits) {
    VerifyFixture fixture;
    const VerifyReport report = verify_repository(fixture.objects, fixture.refs);
    FORGE_CHECK(report.ok());
    FORGE_CHECK(report.objects_checked == 0);
}

FORGE_TEST_CASE(verify_detects_a_corrupted_object) {
    VerifyFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "content");
    fixture.stage_everything();
    const ObjectId commit_id = fixture.commit("first");

    const std::string hex = commit_id.to_hex();
    const std::filesystem::path object_path = fixture.objects_dir.path() / hex.substr(0, 2) / hex.substr(2);
    {
        std::ofstream corrupt(object_path, std::ios::binary | std::ios::trunc);
        corrupt << "not the original commit bytes";
    }

    const VerifyReport report = verify_repository(fixture.objects, fixture.refs);
    FORGE_CHECK(!report.ok());
    FORGE_CHECK(report.corrupt_objects.size() == 1);
    FORGE_CHECK(report.corrupt_objects.at(0).find(hex) != std::string::npos);
}

FORGE_TEST_CASE(verify_detects_a_missing_object_referenced_by_a_branch) {
    VerifyFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "content");
    fixture.stage_everything();
    fixture.commit("first");

    const ObjectId head_commit = *fixture.refs.resolve_head();
    const std::string tree_hex = fixture.objects.get_commit(head_commit).tree_id.to_hex();
    const std::filesystem::path tree_path = fixture.objects_dir.path() / tree_hex.substr(0, 2) / tree_hex.substr(2);
    std::filesystem::remove(tree_path);

    const VerifyReport report = verify_repository(fixture.objects, fixture.refs);
    FORGE_CHECK(!report.ok());
    FORGE_CHECK(report.missing_objects.size() == 1);
    FORGE_CHECK(report.missing_objects.at(0).find(tree_hex) != std::string::npos);
    FORGE_CHECK(report.missing_objects.at(0).find("main") != std::string::npos);
}

FORGE_TEST_CASE(verify_reports_an_orphaned_temp_file_without_failing) {
    VerifyFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "content");
    fixture.stage_everything();
    fixture.commit("first");

    std::filesystem::create_directories(fixture.objects_dir.path() / "ab");
    write_file(fixture.objects_dir.path() / "ab" / "leftover.tmp-1234", "partial write");

    const VerifyReport report = verify_repository(fixture.objects, fixture.refs);
    FORGE_CHECK(report.ok()); // orphaned temp files are harmless, not a failure
    FORGE_CHECK(report.orphaned_files.size() == 1);
}

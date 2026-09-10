#include <fstream>
#include <sstream>

#include "cli/cli.hpp"
#include "core/branching.hpp"
#include "core/checkout.hpp"
#include "core/committing.hpp"
#include "core/error.hpp"
#include "core/ignore_rules.hpp"
#include "core/merge.hpp"
#include "core/object_id.hpp"
#include "core/staging.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::checkout;
using forge::core::create_branch;
using forge::core::create_commit;
using forge::core::find_merge_base;
using forge::core::IgnoreRules;
using forge::core::merge;
using forge::core::MergeOutcome;
using forge::core::MergeResult;
using forge::core::ObjectId;
using forge::core::resolve_checkout_target;
using forge::core::stage_path;
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

struct MergeFixture {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects{objects_dir.path()};
    IndexStore index_store{repo.path() / ".forge" / "index"};
    RefStore refs{repo.path() / ".forge"};
    IgnoreRules ignore_rules = IgnoreRules::parse("");

    MergeFixture() { forge::storage::initialize_repository(repo.path()); }

    void stage_everything() { stage_path(objects, index_store, ignore_rules, repo.path(), repo.path()); }

    ObjectId commit(const std::string& message) {
        return create_commit(objects, index_store, refs, "Test <t@example.com>", message, 1000).commit_id;
    }

    void switch_to(const std::string& branch) {
        checkout(objects, index_store, refs, repo.path(), *resolve_checkout_target(refs, objects, branch));
    }
};

} // namespace

FORGE_TEST_CASE(parse_args_merge_reads_target_and_message) {
    const auto result = forge::cli::parse_args({"merge", "feature", "-m", "custom message"});
    FORGE_CHECK(result.command == forge::cli::Command::Merge);
    FORGE_CHECK(result.merge_target == "feature");
    FORGE_CHECK(result.merge_message == "custom message");
}

FORGE_TEST_CASE(parse_args_merge_without_message_leaves_it_empty) {
    const auto result = forge::cli::parse_args({"merge", "feature"});
    FORGE_CHECK(result.command == forge::cli::Command::Merge);
    FORGE_CHECK(result.merge_target == "feature");
    FORGE_CHECK(result.merge_message.empty());
}

FORGE_TEST_CASE(find_merge_base_finds_nearest_common_ancestor) {
    MergeFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    const ObjectId base_commit = fixture.commit("base");
    create_branch(fixture.refs, "feature");

    write_file(fixture.repo.path() / "a.txt", "on main");
    fixture.stage_everything();
    const ObjectId main_tip = fixture.commit("main change");

    fixture.switch_to("feature");
    write_file(fixture.repo.path() / "b.txt", "on feature");
    fixture.stage_everything();
    const ObjectId feature_tip = fixture.commit("feature change");

    FORGE_CHECK(find_merge_base(fixture.objects, main_tip, feature_tip) == base_commit);
}

FORGE_TEST_CASE(merge_reports_already_up_to_date_when_target_is_an_ancestor) {
    MergeFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("base");
    create_branch(fixture.refs, "feature");

    write_file(fixture.repo.path() / "a.txt", "on main");
    fixture.stage_everything();
    fixture.commit("main change");

    const MergeResult result = merge(
        fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), "feature", "Test <t@example.com>",
        "merge", 2000);
    FORGE_CHECK(result.outcome == MergeOutcome::AlreadyUpToDate);
}

FORGE_TEST_CASE(merge_fast_forwards_when_head_is_an_ancestor_of_target) {
    MergeFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("base");
    create_branch(fixture.refs, "feature");

    fixture.switch_to("feature");
    write_file(fixture.repo.path() / "b.txt", "on feature");
    fixture.stage_everything();
    const ObjectId feature_tip = fixture.commit("feature change");
    fixture.switch_to("main");

    const MergeResult result = merge(
        fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), "feature", "Test <t@example.com>",
        "merge", 2000);
    FORGE_CHECK(result.outcome == MergeOutcome::FastForward);
    FORGE_CHECK(result.commit_id == feature_tip);
    FORGE_CHECK(fixture.refs.read_branch("main") == feature_tip);
    FORGE_CHECK(std::filesystem::exists(fixture.repo.path() / "b.txt"));
}

FORGE_TEST_CASE(merge_creates_merge_commit_for_a_clean_three_way_merge) {
    MergeFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("base");
    create_branch(fixture.refs, "feature");

    write_file(fixture.repo.path() / "a.txt", "changed on main");
    fixture.stage_everything();
    const ObjectId main_tip = fixture.commit("main change");

    fixture.switch_to("feature");
    write_file(fixture.repo.path() / "b.txt", "on feature");
    fixture.stage_everything();
    const ObjectId feature_tip = fixture.commit("feature change");
    fixture.switch_to("main");

    const MergeResult result = merge(
        fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), "feature", "Test <t@example.com>",
        "merge feature", 2000);
    FORGE_CHECK(result.outcome == MergeOutcome::Merged);
    FORGE_CHECK(result.conflicts.empty());
    FORGE_CHECK(read_file(fixture.repo.path() / "a.txt") == "changed on main");
    FORGE_CHECK(read_file(fixture.repo.path() / "b.txt") == "on feature");

    const auto merge_commit = fixture.objects.get_commit(*result.commit_id);
    FORGE_CHECK(merge_commit.parent_ids.size() == 2);
    FORGE_CHECK(merge_commit.parent_ids.at(0) == main_tip);
    FORGE_CHECK(merge_commit.parent_ids.at(1) == feature_tip);
    FORGE_CHECK(fixture.refs.read_branch("main") == result.commit_id);
}

FORGE_TEST_CASE(merge_reports_conflict_when_both_sides_edit_the_same_file_differently) {
    MergeFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "base content");
    fixture.stage_everything();
    fixture.commit("base");
    create_branch(fixture.refs, "feature");

    write_file(fixture.repo.path() / "a.txt", "main version");
    fixture.stage_everything();
    const ObjectId main_tip = fixture.commit("main change");

    fixture.switch_to("feature");
    write_file(fixture.repo.path() / "a.txt", "feature version");
    fixture.stage_everything();
    fixture.commit("feature change");
    fixture.switch_to("main");

    const MergeResult result = merge(
        fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), "feature", "Test <t@example.com>",
        "merge feature", 2000);
    FORGE_CHECK(result.outcome == MergeOutcome::Conflict);
    FORGE_CHECK(result.conflicts.size() == 1);
    FORGE_CHECK(result.conflicts.at(0).path == "a.txt");

    const std::string on_disk = read_file(fixture.repo.path() / "a.txt");
    FORGE_CHECK(on_disk.find("<<<<<<< ours") != std::string::npos);
    FORGE_CHECK(on_disk.find("main version") != std::string::npos);
    FORGE_CHECK(on_disk.find("feature version") != std::string::npos);
    FORGE_CHECK(on_disk.find(">>>>>>> theirs") != std::string::npos);

    // Nothing committed or moved: main's branch pointer is untouched.
    FORGE_CHECK(fixture.refs.read_branch("main") == main_tip);
}

FORGE_TEST_CASE(merge_with_staged_uncommitted_changes_throws) {
    MergeFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("base");
    create_branch(fixture.refs, "feature");

    fixture.switch_to("feature");
    write_file(fixture.repo.path() / "b.txt", "on feature");
    fixture.stage_everything();
    fixture.commit("feature change");
    fixture.switch_to("main");

    write_file(fixture.repo.path() / "a.txt", "staged but not committed");
    fixture.stage_everything();

    bool threw = false;
    try {
        merge(
            fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), "feature",
            "Test <t@example.com>", "merge feature", 2000);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(merge_rejects_unknown_target) {
    MergeFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("base");

    bool threw = false;
    try {
        merge(
            fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), "nope", "Test <t@example.com>",
            "merge nope", 2000);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

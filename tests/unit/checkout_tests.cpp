#include <fstream>
#include <sstream>

#include "core/blob.hpp"
#include "core/branching.hpp"
#include "core/checkout.hpp"
#include "core/committing.hpp"
#include "core/error.hpp"
#include "core/ignore_rules.hpp"
#include "core/object_id.hpp"
#include "core/staging.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::checkout;
using forge::core::CheckoutResult;
using forge::core::CheckoutTargetKind;
using forge::core::create_branch;
using forge::core::create_commit;
using forge::core::IgnoreRules;
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

struct CheckoutFixture {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects{objects_dir.path()};
    IndexStore index_store{repo.path() / ".forge" / "index"};
    RefStore refs{repo.path() / ".forge"};
    IgnoreRules ignore_rules = IgnoreRules::parse("");

    CheckoutFixture() { forge::storage::initialize_repository(repo.path()); }

    void stage_everything() { stage_path(objects, index_store, ignore_rules, repo.path(), repo.path()); }

    ObjectId commit(const std::string& message) {
        return create_commit(objects, index_store, refs, "Test <t@example.com>", message, 1000).commit_id;
    }
};

} // namespace

FORGE_TEST_CASE(resolve_checkout_target_finds_existing_branch) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    const ObjectId commit_id = fixture.commit("first");

    const auto target = resolve_checkout_target(fixture.refs, fixture.objects, "main");
    FORGE_CHECK(target.has_value());
    FORGE_CHECK(target->kind == CheckoutTargetKind::Branch);
    FORGE_CHECK(target->commit_id == commit_id);
}

FORGE_TEST_CASE(resolve_checkout_target_finds_raw_commit_hash) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    const ObjectId commit_id = fixture.commit("first");

    const auto target = resolve_checkout_target(fixture.refs, fixture.objects, commit_id.to_hex());
    FORGE_CHECK(target.has_value());
    FORGE_CHECK(target->kind == CheckoutTargetKind::DetachedCommit);
    FORGE_CHECK(target->commit_id == commit_id);
}

FORGE_TEST_CASE(resolve_checkout_target_returns_nullopt_for_unknown_name) {
    CheckoutFixture fixture;
    FORGE_CHECK(!resolve_checkout_target(fixture.refs, fixture.objects, "nope").has_value());
}

FORGE_TEST_CASE(resolve_checkout_target_rejects_non_commit_object) {
    CheckoutFixture fixture;
    const ObjectId blob_id = fixture.objects.put_blob(forge::core::Blob{"content"});
    FORGE_CHECK(!resolve_checkout_target(fixture.refs, fixture.objects, blob_id.to_hex()).has_value());
}

FORGE_TEST_CASE(checkout_switches_working_tree_between_branches) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "on main");
    fixture.stage_everything();
    fixture.commit("main commit");
    create_branch(fixture.refs, "feature");

    const auto to_feature = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_feature);
    FORGE_CHECK(*fixture.refs.read_head().branch == "feature");

    write_file(fixture.repo.path() / "b.txt", "on feature");
    fixture.stage_everything();
    fixture.commit("feature commit");

    const auto to_main = *resolve_checkout_target(fixture.refs, fixture.objects, "main");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_main);

    FORGE_CHECK(std::filesystem::exists(fixture.repo.path() / "a.txt"));
    FORGE_CHECK(!std::filesystem::exists(fixture.repo.path() / "b.txt"));
    FORGE_CHECK(*fixture.refs.read_head().branch == "main");
}

FORGE_TEST_CASE(checkout_prunes_empty_directories_left_by_removed_files) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "sub" / "x.txt", "x");
    fixture.stage_everything();
    fixture.commit("first");
    create_branch(fixture.refs, "feature");

    std::filesystem::remove(fixture.repo.path() / "sub" / "x.txt");
    fixture.stage_everything();
    fixture.commit("remove x");

    const auto to_feature = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_feature);
    FORGE_CHECK(std::filesystem::exists(fixture.repo.path() / "sub" / "x.txt"));

    const auto to_main = *resolve_checkout_target(fixture.refs, fixture.objects, "main");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_main);
    FORGE_CHECK(!std::filesystem::exists(fixture.repo.path() / "sub"));
}

FORGE_TEST_CASE(checkout_with_staged_uncommitted_changes_throws) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("first");
    create_branch(fixture.refs, "feature");

    write_file(fixture.repo.path() / "a.txt", "modified");
    fixture.stage_everything();

    const auto target = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    bool threw = false;
    try {
        checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), target);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(checkout_with_unstaged_local_modification_throws_and_touches_nothing) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("first");
    create_branch(fixture.refs, "feature");

    // Advance "feature" past "main" so a.txt actually differs between the
    // two — switching to a target with an identical tree can never
    // conflict, so the test needs a real divergence to exercise the check.
    const auto to_feature = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_feature);
    write_file(fixture.repo.path() / "a.txt", "changed on feature");
    fixture.stage_everything();
    fixture.commit("feature changes a.txt");

    const auto to_main = *resolve_checkout_target(fixture.refs, fixture.objects, "main");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_main);

    write_file(fixture.repo.path() / "a.txt", "modified but not staged");

    // Re-resolve "feature": its tip advanced after `to_feature` was
    // captured above, and checkout must diff against its current tree.
    const auto target = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    bool threw = false;
    try {
        checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), target);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
    FORGE_CHECK(read_file(fixture.repo.path() / "a.txt") == "modified but not staged");
}

FORGE_TEST_CASE(checkout_blocked_by_untracked_file_at_target_path) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("first");
    create_branch(fixture.refs, "feature");

    const auto to_feature_before_new_file = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_feature_before_new_file);
    write_file(fixture.repo.path() / "new.txt", "on feature");
    fixture.stage_everything();
    fixture.commit("feature adds new.txt");

    const auto to_main = *resolve_checkout_target(fixture.refs, fixture.objects, "main");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_main);
    FORGE_CHECK(!std::filesystem::exists(fixture.repo.path() / "new.txt"));

    write_file(fixture.repo.path() / "new.txt", "untracked, unrelated content");

    // Re-resolve "feature": it now points at the commit that adds
    // new.txt, not the stale pre-advance commit captured above.
    const auto to_feature = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    bool threw = false;
    try {
        checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_feature);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(checkout_ignores_unrelated_untracked_files) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("first");
    create_branch(fixture.refs, "feature");

    write_file(fixture.repo.path() / "untracked.txt", "not part of any commit");

    const auto target = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    bool threw = false;
    try {
        checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), target);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(!threw);
    FORGE_CHECK(std::filesystem::exists(fixture.repo.path() / "untracked.txt"));
}

FORGE_TEST_CASE(checkout_detaches_head_when_target_is_a_raw_commit) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    const ObjectId commit_id = fixture.commit("first");

    const auto target = *resolve_checkout_target(fixture.refs, fixture.objects, commit_id.to_hex());
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), target);

    const RefStore::Head head = fixture.refs.read_head();
    FORGE_CHECK(!head.branch.has_value());
    FORGE_CHECK(head.detached_commit == commit_id);
}

FORGE_TEST_CASE(checkout_result_reports_updated_and_removed_paths) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("first");
    create_branch(fixture.refs, "feature");

    const auto to_feature = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_feature);
    write_file(fixture.repo.path() / "b.txt", "b");
    fixture.stage_everything();
    fixture.commit("feature adds b");

    const auto to_main = *resolve_checkout_target(fixture.refs, fixture.objects, "main");
    const CheckoutResult result =
        checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_main);
    FORGE_CHECK(result.removed.size() == 1);
    FORGE_CHECK(result.removed.at(0) == "b.txt");
    FORGE_CHECK(result.updated.empty()); // a.txt identical on both branches, untouched
}

FORGE_TEST_CASE(checkout_replaces_index_to_match_target_tree) {
    CheckoutFixture fixture;
    write_file(fixture.repo.path() / "a.txt", "a");
    fixture.stage_everything();
    fixture.commit("first");
    create_branch(fixture.refs, "feature");

    const auto to_feature = *resolve_checkout_target(fixture.refs, fixture.objects, "feature");
    checkout(fixture.objects, fixture.index_store, fixture.refs, fixture.repo.path(), to_feature);

    const auto index = fixture.index_store.load();
    FORGE_CHECK(index.entries().size() == 1);
    FORGE_CHECK(index.find("a.txt").has_value());
}

#include "core/error.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::ObjectId;
using forge::storage::RefStore;
using forge::test::TempDir;

namespace {

struct RefStoreFixture {
    TempDir dir;
    RefStore refs{dir.path() / forge::storage::kForgeDirName};
};

} // namespace

FORGE_TEST_CASE(fresh_repo_head_is_attached_to_unborn_default_branch) {
    RefStoreFixture fixture;
    forge::storage::initialize_repository(fixture.dir.path());

    const RefStore::Head head = fixture.refs.read_head();
    FORGE_CHECK(head.branch.has_value());
    FORGE_CHECK(*head.branch == "main");
    FORGE_CHECK(!head.detached_commit.has_value());
    FORGE_CHECK(!fixture.refs.resolve_head().has_value());
}

FORGE_TEST_CASE(read_branch_returns_nullopt_for_missing_branch) {
    RefStoreFixture fixture;
    FORGE_CHECK(!fixture.refs.read_branch("main").has_value());
    FORGE_CHECK(!fixture.refs.branch_exists("main"));
}

FORGE_TEST_CASE(update_branch_creates_new_branch_when_expected_old_is_nullopt) {
    RefStoreFixture fixture;
    const ObjectId commit_id = ObjectId::of("commit-1");

    fixture.refs.update_branch("main", std::nullopt, commit_id);

    FORGE_CHECK(fixture.refs.branch_exists("main"));
    FORGE_CHECK(fixture.refs.read_branch("main") == commit_id);
}

FORGE_TEST_CASE(update_branch_creating_an_existing_branch_throws) {
    RefStoreFixture fixture;
    fixture.refs.update_branch("main", std::nullopt, ObjectId::of("commit-1"));

    bool threw = false;
    try {
        fixture.refs.update_branch("main", std::nullopt, ObjectId::of("commit-2"));
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(update_branch_with_matching_expected_old_advances_it) {
    RefStoreFixture fixture;
    const ObjectId first = ObjectId::of("commit-1");
    const ObjectId second = ObjectId::of("commit-2");
    fixture.refs.update_branch("main", std::nullopt, first);

    fixture.refs.update_branch("main", first, second);
    FORGE_CHECK(fixture.refs.read_branch("main") == second);
}

FORGE_TEST_CASE(update_branch_with_stale_expected_old_throws) {
    RefStoreFixture fixture;
    const ObjectId first = ObjectId::of("commit-1");
    const ObjectId stale = ObjectId::of("commit-stale");
    const ObjectId attempted = ObjectId::of("commit-2");
    fixture.refs.update_branch("main", std::nullopt, first);

    bool threw = false;
    try {
        fixture.refs.update_branch("main", stale, attempted);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
    FORGE_CHECK(fixture.refs.read_branch("main") == first); // rejected update never applied
}

FORGE_TEST_CASE(list_branches_returns_sorted_names) {
    RefStoreFixture fixture;
    const ObjectId commit_id = ObjectId::of("commit-1");
    fixture.refs.update_branch("zebra", std::nullopt, commit_id);
    fixture.refs.update_branch("apple", std::nullopt, commit_id);

    const std::vector<std::string> names = fixture.refs.list_branches();
    FORGE_CHECK(names.size() == 2);
    FORGE_CHECK(names.at(0) == "apple");
    FORGE_CHECK(names.at(1) == "zebra");
}

FORGE_TEST_CASE(set_head_branch_changes_which_branch_head_tracks) {
    RefStoreFixture fixture;
    forge::storage::initialize_repository(fixture.dir.path());
    fixture.refs.update_branch("feature", std::nullopt, ObjectId::of("commit-1"));

    fixture.refs.set_head_branch("feature");

    const RefStore::Head head = fixture.refs.read_head();
    FORGE_CHECK(head.branch.has_value());
    FORGE_CHECK(*head.branch == "feature");
    FORGE_CHECK(fixture.refs.resolve_head() == ObjectId::of("commit-1"));
}

FORGE_TEST_CASE(set_head_detached_makes_head_report_no_branch) {
    RefStoreFixture fixture;
    forge::storage::initialize_repository(fixture.dir.path());
    const ObjectId commit_id = ObjectId::of("commit-1");

    fixture.refs.set_head_detached(commit_id);

    const RefStore::Head head = fixture.refs.read_head();
    FORGE_CHECK(!head.branch.has_value());
    FORGE_CHECK(head.detached_commit == commit_id);
    FORGE_CHECK(fixture.refs.resolve_head() == commit_id);
}

FORGE_TEST_CASE(branch_path_rejects_path_traversal_in_branch_name) {
    RefStoreFixture fixture;
    bool threw = false;
    try {
        fixture.refs.update_branch("../escape", std::nullopt, ObjectId::of("commit-1"));
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

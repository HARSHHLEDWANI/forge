#include "core/branching.hpp"
#include "core/error.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::BranchInfo;
using forge::core::create_branch;
using forge::core::list_branches;
using forge::core::ObjectId;
using forge::storage::RefStore;
using forge::test::TempDir;

namespace {

struct BranchingFixture {
    TempDir dir;
    RefStore refs{dir.path() / forge::storage::kForgeDirName};

    BranchingFixture() { forge::storage::initialize_repository(dir.path()); }
};

} // namespace

FORGE_TEST_CASE(create_branch_on_unborn_head_throws) {
    BranchingFixture fixture;
    bool threw = false;
    try {
        create_branch(fixture.refs, "feature");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(create_branch_points_at_current_head_commit) {
    BranchingFixture fixture;
    const ObjectId commit_id = ObjectId::of("commit-1");
    fixture.refs.update_branch("main", std::nullopt, commit_id);

    create_branch(fixture.refs, "feature");
    FORGE_CHECK(fixture.refs.read_branch("feature") == commit_id);
}

FORGE_TEST_CASE(create_branch_does_not_move_head) {
    BranchingFixture fixture;
    fixture.refs.update_branch("main", std::nullopt, ObjectId::of("commit-1"));

    create_branch(fixture.refs, "feature");
    FORGE_CHECK(*fixture.refs.read_head().branch == "main");
}

FORGE_TEST_CASE(create_branch_rejects_duplicate_name) {
    BranchingFixture fixture;
    fixture.refs.update_branch("main", std::nullopt, ObjectId::of("commit-1"));
    create_branch(fixture.refs, "feature");

    bool threw = false;
    try {
        create_branch(fixture.refs, "feature");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(create_branch_rejects_empty_name) {
    BranchingFixture fixture;
    fixture.refs.update_branch("main", std::nullopt, ObjectId::of("commit-1"));

    bool threw = false;
    try {
        create_branch(fixture.refs, "");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(create_branch_rejects_name_with_slash) {
    BranchingFixture fixture;
    fixture.refs.update_branch("main", std::nullopt, ObjectId::of("commit-1"));

    bool threw = false;
    try {
        create_branch(fixture.refs, "feature/x");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(create_branch_rejects_reserved_head_name) {
    BranchingFixture fixture;
    fixture.refs.update_branch("main", std::nullopt, ObjectId::of("commit-1"));

    bool threw = false;
    try {
        create_branch(fixture.refs, "HEAD");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(list_branches_marks_current_branch) {
    BranchingFixture fixture;
    fixture.refs.update_branch("main", std::nullopt, ObjectId::of("commit-1"));
    create_branch(fixture.refs, "feature");

    const std::vector<BranchInfo> branches = list_branches(fixture.refs);
    FORGE_CHECK(branches.size() == 2);
    for (const BranchInfo& branch : branches) {
        FORGE_CHECK(branch.is_current == (branch.name == "main"));
    }
}

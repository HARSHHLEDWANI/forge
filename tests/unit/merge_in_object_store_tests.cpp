#include <optional>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "core/merge.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::Blob;
using forge::core::Commit;
using forge::core::EntryMode;
using forge::core::merge_in_object_store;
using forge::core::MergeOutcome;
using forge::core::ObjectId;
using forge::core::Tree;
using forge::core::TreeEntry;
using forge::storage::ObjectStore;
using forge::storage::RefStore;
using forge::test::TempDir;

namespace {

// A repository with no working tree at all — exactly how the server
// hosts one (server/repo_registry.hpp) — so every commit here is built
// directly through the object store, the same way
// database/collaboration_directory.hpp's PR-merge caller would.
struct BareRepoFixture {
    TempDir objects_dir;
    TempDir refs_dir;
    ObjectStore objects{objects_dir.path()};
    RefStore refs{refs_dir.path()};

    ObjectId commit_with_file(
        const std::string& filename, const std::string& content, const std::vector<ObjectId>& parents,
        std::int64_t timestamp) {
        const ObjectId blob_id = objects.put_blob(Blob{content});
        const Tree tree(std::vector<TreeEntry>{{filename, EntryMode::RegularFile, blob_id}});
        const ObjectId tree_id = objects.put_tree(tree);
        const Commit commit{tree_id, parents, "Test <t@example.com>", timestamp, "commit"};
        return objects.put_commit(commit);
    }

    ObjectId commit_with_files(
        const std::vector<std::pair<std::string, std::string>>& files, const std::vector<ObjectId>& parents,
        std::int64_t timestamp) {
        std::vector<TreeEntry> entries;
        for (const auto& [name, content] : files) {
            entries.push_back(TreeEntry{name, EntryMode::RegularFile, objects.put_blob(Blob{content})});
        }
        const Tree tree(entries);
        const ObjectId tree_id = objects.put_tree(tree);
        const Commit commit{tree_id, parents, "Test <t@example.com>", timestamp, "commit"};
        return objects.put_commit(commit);
    }
};

} // namespace

FORGE_TEST_CASE(merge_in_object_store_fast_forwards_when_target_is_an_ancestor) {
    BareRepoFixture fixture;
    const ObjectId base = fixture.commit_with_file("a.txt", "base", {}, 1000);
    fixture.refs.update_branch("main", std::nullopt, base);
    const ObjectId ahead = fixture.commit_with_file("b.txt", "new", {base}, 1001);

    const auto result = merge_in_object_store(
        fixture.objects, fixture.refs, ahead, "main", "Test <t@example.com>", "merge", 2000);
    FORGE_CHECK(result.outcome == MergeOutcome::FastForward);
    FORGE_CHECK(result.commit_id == ahead);
    FORGE_CHECK(fixture.refs.read_branch("main") == ahead);
}

FORGE_TEST_CASE(merge_in_object_store_reports_already_up_to_date) {
    BareRepoFixture fixture;
    const ObjectId base = fixture.commit_with_file("a.txt", "base", {}, 1000);
    const ObjectId ahead = fixture.commit_with_file("b.txt", "new", {base}, 1001);
    fixture.refs.update_branch("main", std::nullopt, ahead);

    const auto result = merge_in_object_store(
        fixture.objects, fixture.refs, base, "main", "Test <t@example.com>", "merge", 2000);
    FORGE_CHECK(result.outcome == MergeOutcome::AlreadyUpToDate);
    FORGE_CHECK(fixture.refs.read_branch("main") == ahead); // untouched
}

FORGE_TEST_CASE(merge_in_object_store_creates_a_merge_commit_for_a_clean_three_way_merge) {
    BareRepoFixture fixture;
    const ObjectId base = fixture.commit_with_file("a.txt", "base", {}, 1000);
    fixture.refs.update_branch("main", std::nullopt, base);
    const ObjectId target_tip = fixture.commit_with_files({{"a.txt", "changed on main"}}, {base}, 1001);
    fixture.refs.update_branch("main", base, target_tip);
    const ObjectId source_tip = fixture.commit_with_files({{"a.txt", "base"}, {"b.txt", "from branch"}}, {base}, 1002);

    const auto result = merge_in_object_store(
        fixture.objects, fixture.refs, source_tip, "main", "Test <t@example.com>", "merge branch", 2000);
    FORGE_CHECK(result.outcome == MergeOutcome::Merged);
    FORGE_CHECK(result.conflicts.empty());
    FORGE_CHECK(fixture.refs.read_branch("main") == result.commit_id);

    const auto merge_commit = fixture.objects.get_commit(*result.commit_id);
    FORGE_CHECK(merge_commit.parent_ids.size() == 2);
    FORGE_CHECK(merge_commit.parent_ids.at(0) == target_tip);
    FORGE_CHECK(merge_commit.parent_ids.at(1) == source_tip);
}

FORGE_TEST_CASE(merge_in_object_store_reports_a_conflict_without_creating_a_commit) {
    BareRepoFixture fixture;
    const ObjectId base = fixture.commit_with_file("a.txt", "base content", {}, 1000);
    fixture.refs.update_branch("main", std::nullopt, base);
    const ObjectId target_tip = fixture.commit_with_file("a.txt", "main version", {base}, 1001);
    fixture.refs.update_branch("main", base, target_tip);
    const ObjectId source_tip = fixture.commit_with_file("a.txt", "branch version", {base}, 1002);

    const auto result = merge_in_object_store(
        fixture.objects, fixture.refs, source_tip, "main", "Test <t@example.com>", "merge branch", 2000);
    FORGE_CHECK(result.outcome == MergeOutcome::Conflict);
    FORGE_CHECK(result.conflicts.size() == 1);
    FORGE_CHECK(result.conflicts.at(0).path == "a.txt");
    FORGE_CHECK(fixture.refs.read_branch("main") == target_tip); // untouched
}

FORGE_TEST_CASE(merge_in_object_store_rejects_a_missing_target_branch) {
    BareRepoFixture fixture;
    const ObjectId commit_id = fixture.commit_with_file("a.txt", "content", {}, 1000);
    bool threw = false;
    try {
        merge_in_object_store(fixture.objects, fixture.refs, commit_id, "nope", "Test <t@example.com>", "merge", 2000);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

#include <fstream>

#include "core/blob.hpp"
#include "core/commit.hpp"
#include "core/committing.hpp"
#include "core/error.hpp"
#include "core/index.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::Blob;
using forge::core::CommitResult;
using forge::core::create_commit;
using forge::core::EntryMode;
using forge::core::Index;
using forge::core::IndexEntry;
using forge::storage::IndexStore;
using forge::storage::ObjectStore;
using forge::storage::RefStore;
using forge::test::TempDir;

namespace {

struct CommittingFixture {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects{objects_dir.path()};
    IndexStore index_store{repo.path() / "index"};
    RefStore refs{repo.path() / forge::storage::kForgeDirName};

    CommittingFixture() { forge::storage::initialize_repository(repo.path()); }

    void stage(const std::string& path, const std::string& content) {
        const auto blob_id = objects.put_blob(Blob{content});
        Index index = index_store.load();
        index.upsert(IndexEntry{path, EntryMode::RegularFile, blob_id});
        index_store.save(index);
    }
};

} // namespace

FORGE_TEST_CASE(create_commit_on_unborn_branch_has_no_parents) {
    CommittingFixture fixture;
    fixture.stage("a.txt", "hello");

    const CommitResult result =
        create_commit(fixture.objects, fixture.index_store, fixture.refs, "A <a@example.com>", "initial", 1000);

    const auto commit = fixture.objects.get_commit(result.commit_id);
    FORGE_CHECK(commit.parent_ids.empty());
    FORGE_CHECK(commit.tree_id == result.tree_id);
    FORGE_CHECK(commit.message == "initial");
}

FORGE_TEST_CASE(create_commit_advances_current_branch) {
    CommittingFixture fixture;
    fixture.stage("a.txt", "hello");

    const CommitResult result =
        create_commit(fixture.objects, fixture.index_store, fixture.refs, "A <a@example.com>", "initial", 1000);

    FORGE_CHECK(fixture.refs.read_branch("main") == result.commit_id);
}

FORGE_TEST_CASE(create_commit_second_commit_has_first_as_parent) {
    CommittingFixture fixture;
    fixture.stage("a.txt", "hello");
    const CommitResult first =
        create_commit(fixture.objects, fixture.index_store, fixture.refs, "A <a@example.com>", "first", 1000);

    fixture.stage("b.txt", "world");
    const CommitResult second =
        create_commit(fixture.objects, fixture.index_store, fixture.refs, "A <a@example.com>", "second", 1001);

    const auto second_commit = fixture.objects.get_commit(second.commit_id);
    FORGE_CHECK(second_commit.parent_ids.size() == 1);
    FORGE_CHECK(second_commit.parent_ids.at(0) == first.commit_id);
}

FORGE_TEST_CASE(create_commit_with_unchanged_tree_throws) {
    CommittingFixture fixture;
    fixture.stage("a.txt", "hello");
    create_commit(fixture.objects, fixture.index_store, fixture.refs, "A <a@example.com>", "first", 1000);

    bool threw = false;
    try {
        create_commit(fixture.objects, fixture.index_store, fixture.refs, "A <a@example.com>", "second", 1001);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(create_commit_rejects_empty_message) {
    CommittingFixture fixture;
    fixture.stage("a.txt", "hello");

    bool threw = false;
    try {
        create_commit(fixture.objects, fixture.index_store, fixture.refs, "A <a@example.com>", "", 1000);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(create_commit_rejects_empty_author) {
    CommittingFixture fixture;
    fixture.stage("a.txt", "hello");

    bool threw = false;
    try {
        create_commit(fixture.objects, fixture.index_store, fixture.refs, "", "message", 1000);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(create_commit_with_detached_head_throws) {
    CommittingFixture fixture;
    fixture.stage("a.txt", "hello");
    // Detach HEAD by writing a raw commit id directly, bypassing
    // set_head_branch (which only supports symbolic targets) since
    // detached HEAD itself is Phase 7 scope — this only exercises
    // create_commit's own guard against it.
    std::ofstream head_file((fixture.repo.path() / ".forge" / "HEAD").string(), std::ios::binary | std::ios::trunc);
    head_file << forge::core::ObjectId::of("dummy-not-a-real-commit").to_hex() << '\n';
    head_file.close();

    bool threw = false;
    try {
        create_commit(fixture.objects, fixture.index_store, fixture.refs, "A <a@example.com>", "msg", 1000);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

#include "core/commit.hpp"
#include "core/log.hpp"
#include "storage/object_store.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::Commit;
using forge::core::commit_log;
using forge::core::LogEntry;
using forge::core::ObjectId;
using forge::storage::ObjectStore;
using forge::test::TempDir;

FORGE_TEST_CASE(commit_log_single_commit_yields_one_entry) {
    TempDir dir;
    ObjectStore objects(dir.path());
    const Commit commit{ObjectId::of("tree"), {}, "A <a@example.com>", 1000, "initial"};
    const ObjectId id = objects.put_commit(commit);

    const std::vector<LogEntry> entries = commit_log(objects, id);
    FORGE_CHECK(entries.size() == 1);
    FORGE_CHECK(entries.at(0).id == id);
    FORGE_CHECK(entries.at(0).commit.message == "initial");
}

FORGE_TEST_CASE(commit_log_walks_first_parent_chain_most_recent_first) {
    TempDir dir;
    ObjectStore objects(dir.path());
    const ObjectId first_id = objects.put_commit(Commit{ObjectId::of("tree1"), {}, "A <a@example.com>", 1000, "first"});
    const ObjectId second_id =
        objects.put_commit(Commit{ObjectId::of("tree2"), {first_id}, "A <a@example.com>", 1001, "second"});
    const ObjectId third_id =
        objects.put_commit(Commit{ObjectId::of("tree3"), {second_id}, "A <a@example.com>", 1002, "third"});

    const std::vector<LogEntry> entries = commit_log(objects, third_id);
    FORGE_CHECK(entries.size() == 3);
    FORGE_CHECK(entries.at(0).id == third_id);
    FORGE_CHECK(entries.at(1).id == second_id);
    FORGE_CHECK(entries.at(2).id == first_id);
}

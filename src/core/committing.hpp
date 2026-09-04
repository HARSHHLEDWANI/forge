#pragma once

#include <cstdint>
#include <string_view>

#include "core/object_id.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"

namespace forge::core {

struct CommitResult {
    ObjectId commit_id;
    ObjectId tree_id;
};

// Snapshots the current index into a commit: builds a Tree from the
// staged entries, wraps it with `author`/`message`/`timestamp` and the
// current branch's commit as sole parent (no parent if this is the first
// commit on an unborn branch), stores the new Commit object, then
// atomically advances HEAD's branch to point at it. The ref update is a
// compare-and-swap (ADR 0003): a concurrent commit that races this one
// and wins is detected and rejected rather than silently overwritten
// (docs/design/failure-scenarios.md item 6).
//
// Throws core::ForgeError if `author` or `message` is empty, if HEAD is
// detached (unsupported until Phase 7 checkout exists to reattach it), or
// if the resulting tree is identical to the parent commit's tree —
// nothing changed since the last commit.
CommitResult create_commit(
    storage::ObjectStore& objects, storage::IndexStore& index_store, storage::RefStore& refs,
    std::string_view author, std::string_view message, std::int64_t timestamp);

} // namespace forge::core

#pragma once

#include <vector>

#include "core/commit.hpp"
#include "core/object_id.hpp"
#include "storage/object_store.hpp"

namespace forge::core {

struct LogEntry {
    ObjectId id;
    Commit commit;
};

// Walks first-parent history starting at `start`, most recent first.
// Every commit currently has at most one parent (merge commits, and
// therefore a second parent, are Phase 9 scope), so first-parent and
// full-history traversal coincide for now.
std::vector<LogEntry> commit_log(const storage::ObjectStore& objects, const ObjectId& start);

} // namespace forge::core

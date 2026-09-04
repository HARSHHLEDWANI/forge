#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/object_id.hpp"
#include "storage/ref_store.hpp"

namespace forge::core {

struct BranchInfo {
    std::string name;
    ObjectId commit_id;
    bool is_current;
};

// All branches, sorted by name, each paired with the commit it currently
// points at and whether it's the branch HEAD is attached to.
std::vector<BranchInfo> list_branches(const storage::RefStore& refs);

// Creates a new branch pointing at HEAD's current commit. Doesn't move
// HEAD — this matches `git branch <name>`, not `git checkout -b <name>`;
// switching HEAD between branches is Phase 7 (Checkout) scope.
//
// Throws core::ForgeError if `name` is empty, contains '/' (nested branch
// namespaces are a deliberate V1 simplification, same spirit as
// tree.hpp's EntryMode note), is the reserved name "HEAD", the branch
// already exists, or HEAD is unborn (no commits yet to point the new
// branch at).
void create_branch(storage::RefStore& refs, std::string_view name);

} // namespace forge::core

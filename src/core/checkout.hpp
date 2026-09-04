#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/object_id.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"

namespace forge::core {

enum class CheckoutTargetKind { Branch, DetachedCommit };

struct CheckoutTarget {
    CheckoutTargetKind kind;
    std::string branch_name; // set only when kind == Branch
    ObjectId commit_id;      // the branch's tip, or the commit itself when detached
};

// Resolves `ref_or_commit` the way `forge checkout` does: an existing
// branch name first, then a full hex commit id that actually names a
// commit object. nullopt if neither resolves.
std::optional<CheckoutTarget> resolve_checkout_target(
    const storage::RefStore& refs, const storage::ObjectStore& objects, std::string_view ref_or_commit);

struct CheckoutResult {
    std::vector<std::string> updated; // written or overwritten on disk
    std::vector<std::string> removed; // deleted from disk
};

// Switches the working tree, index, and HEAD to `target`.
//
// Safe-overwrite policy (deliberately simpler than real Git's partial
// carry-forward, since there's no diff/merge machinery yet to resolve a
// three-way conflict — see Phase 8/9): checkout requires a fully clean
// state first. It throws core::ForgeError, touching nothing on disk,
// if any of the following holds:
//   - the index has staged changes not yet committed (differs from
//     HEAD's tree);
//   - a path HEAD's tree tracks has been modified or deleted in the
//     working directory since the last commit;
//   - a path the target tree would create already exists on disk
//     (an untracked file that checkout would otherwise clobber).
// Once past that check, files are written/removed to match the target
// tree exactly, the index is replaced with the target tree's flattened
// entries, and HEAD is updated last (set_head_branch for an attached
// branch, set_head_detached for a raw commit).
CheckoutResult checkout(
    storage::ObjectStore& objects, storage::IndexStore& index_store, storage::RefStore& refs,
    const std::filesystem::path& repo_root, const CheckoutTarget& target);

} // namespace forge::core

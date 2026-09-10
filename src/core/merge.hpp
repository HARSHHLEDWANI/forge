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

// Finds a common ancestor of `a` and `b` by walking the full parent DAG
// outward from each (every parent, not just first-parent — merge commits
// mean a commit can have more than one) and returning the first ancestor
// of `a` encountered while walking outward from `b`. With more than one
// lowest common ancestor (a criss-cross merge), this returns *a* valid
// merge base, not necessarily the single best one real Git's recursive
// strategy would compute — a deliberate V1 simplification, same spirit
// as tree.hpp's EntryMode note. nullopt means the two commits share no
// common ancestor at all.
std::optional<ObjectId> find_merge_base(const storage::ObjectStore& objects, const ObjectId& a, const ObjectId& b);

enum class MergeOutcome { AlreadyUpToDate, FastForward, Merged, Conflict };

// One path where both sides changed it differently since the merge
// base. nullopt on `base`/`ours`/`theirs` means the path didn't exist
// (yet, or any more) on that side.
struct MergeConflict {
    std::string path;
    std::optional<ObjectId> base_blob_id;
    std::optional<ObjectId> ours_blob_id;
    std::optional<ObjectId> theirs_blob_id;
};

struct MergeResult {
    MergeOutcome outcome = MergeOutcome::AlreadyUpToDate;
    // Set for FastForward and Merged: the commit HEAD's branch now
    // points at (a fast-forward reuses `their` commit as-is; a real
    // merge is a brand-new two-parent commit).
    std::optional<ObjectId> commit_id;
    std::vector<std::string> updated; // written or overwritten on disk
    std::vector<std::string> removed; // deleted from disk
    // Set only for Conflict. Every conflicting path already has
    // Git-style "<<<<<<< ours / ======= / >>>>>>> theirs" markers
    // written into the working tree for the user to resolve by hand.
    // Nothing is staged or committed automatically: there's no
    // MERGE_HEAD-style persisted merge state (a V1 simplification), so
    // resolving conflicts is a plain edit + `forge add` + `forge
    // commit`, producing an ordinary single-parent commit rather than a
    // two-parent merge commit.
    std::vector<MergeConflict> conflicts;
};

// Merges `their_ref_or_commit` (a branch name or raw hex commit id) into
// HEAD's current branch.
//
// Same clean-tree precondition as checkout (see checkout.hpp): the index
// must match HEAD's tree (no staged changes), and every path this merge
// would actually write to must be unmodified since HEAD or, if newly
// created, absent on disk — otherwise throws core::ForgeError, touching
// nothing. Also throws if HEAD is detached, `author`/`message` is empty,
// `their_ref_or_commit` doesn't resolve to a commit, or the two
// histories share no common ancestor at all (refusing to merge unrelated
// histories).
//
// `author`/`message`/`timestamp` are only used to build the merge commit
// when the outcome ends up Merged; the other three outcomes never create
// a commit.
MergeResult merge(
    storage::ObjectStore& objects, storage::IndexStore& index_store, storage::RefStore& refs,
    const std::filesystem::path& repo_root, std::string_view their_ref_or_commit, std::string_view author,
    std::string_view message, std::int64_t timestamp);

} // namespace forge::core

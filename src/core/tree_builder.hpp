#pragma once

#include <filesystem>

#include "core/index.hpp"
#include "core/object_id.hpp"
#include "storage/object_store.hpp"

namespace forge::core {

// Recursively walks `directory`, storing a Blob for each regular file and
// a Tree for each subdirectory (bottom-up: a directory's Tree is only
// built once all its children's ObjectIds are known), and returns the
// ObjectId of the resulting root Tree.
//
// Symlinks are stored as Symlink-mode entries whose blob content is the
// raw link-target text — never followed, both for correctness (the tree
// should record the symlink itself) and safety (a followed symlink cycle
// would recurse forever). `.forge` is always skipped; it's the
// repository's own metadata, not tracked content.
//
// General .forgeignore filtering is out of scope here — that's Phase 5
// (Index/Staging) — this only ever excludes ".forge" itself.
core::ObjectId build_tree_from_directory(
    storage::ObjectStore& store, const std::filesystem::path& directory);

// Reconstructs the nested Tree hierarchy implied by a flat Index — one
// Tree object per directory level — using the blob ids already recorded
// in the index. Unlike build_tree_from_directory, this never touches the
// filesystem or re-hashes anything: staging already stored each blob and
// recorded its id, so committing only needs to organize those existing
// ids into the right shape.
core::ObjectId build_tree_from_index(storage::ObjectStore& store, const Index& index);

// The inverse of build_tree_from_index: walks a Tree's subtrees
// (reading each from `store`) and flattens them back into a single
// Index whose entry paths are '/'-joined from the nesting. Used by
// checkout to know exactly which paths a target commit's tree implies,
// without touching the working directory.
Index flatten_tree_to_index(const storage::ObjectStore& store, const core::ObjectId& tree_id);

} // namespace forge::core

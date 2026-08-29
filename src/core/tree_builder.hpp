#pragma once

#include <filesystem>

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

} // namespace forge::core

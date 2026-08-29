#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/ignore_rules.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"

namespace forge::core {

struct AddResult {
    std::vector<std::string> staged;  // repo-relative paths added or updated
    std::vector<std::string> removed; // repo-relative paths staged as deleted
};

// Stages `target` (a file or directory, resolved relative to the current
// working directory like a normal CLI pathspec) into the index: hashes
// and stores each matching file as a blob, updates the index, and
// persists it via `index_store`.
//
// A path that's still tracked in the index but no longer exists on disk
// is staged as a deletion rather than erroring — this mirrors `git add`
// on a deleted file. `.forgeignore` rules are honored uniformly for both
// explicit files and directory walks (Forge doesn't replicate Git's
// "explicit add overrides ignore" behavior — a deliberate simplification,
// not an oversight). ".forge" itself is always excluded.
//
// Throws core::ForgeError if `target` is outside `repo_root`, or if it
// matches nothing at all (doesn't exist on disk and isn't tracked).
AddResult stage_path(
    storage::ObjectStore& objects,
    storage::IndexStore& index_store,
    const IgnoreRules& ignore_rules,
    const std::filesystem::path& repo_root,
    const std::filesystem::path& target);

} // namespace forge::core

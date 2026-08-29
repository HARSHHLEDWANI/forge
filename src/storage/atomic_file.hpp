#pragma once

#include <filesystem>
#include <string_view>

namespace forge::storage {

// Durably writes `data` to `target`: write to a sibling temp file, flush
// and sync it to disk, verify by reading it back, then atomically publish
// via rename. `target`'s parent directory must already exist. Throws
// core::ForgeError on any failure; a crash or error before the rename
// leaves `target` untouched and at most an orphaned temp file behind —
// `target` never becomes visible with partial or unverified content.
//
// Known limitation: syncs the temp file's own contents, but does not sync
// the containing directory entry for the rename itself, so durability
// against real power loss (as opposed to a process crash) is not yet
// guaranteed. Full crash-consistency is Phase 11 (Reliability) scope.
void write_file_atomic(const std::filesystem::path& target, std::string_view data);

} // namespace forge::storage

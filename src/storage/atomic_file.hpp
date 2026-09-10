#pragma once

#include <filesystem>
#include <string_view>

namespace forge::storage {

// Durably writes `data` to `target`: write to a sibling temp file, flush
// and sync it to disk, verify by reading it back, atomically publish via
// rename, then sync the containing directory so the rename itself
// survives real power loss (not just a process crash). `target`'s parent
// directory must already exist. Throws core::ForgeError on any failure;
// a crash or error before the rename leaves `target` untouched and at
// most an orphaned temp file behind — `target` never becomes visible
// with partial or unverified content. A crash between the rename and
// the directory sync is still safe: `target` is already fully written
// and verified, so at worst the rename has to be replayed by the
// filesystem's own journal, never lost data. core::verify_repository
// (Phase 11) reports any orphaned temp file left by an interrupted write
// so it can be spotted and cleaned up.
void write_file_atomic(const std::filesystem::path& target, std::string_view data);

} // namespace forge::storage

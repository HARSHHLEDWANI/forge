#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace forge::storage {

// Resolves `relative` against `root`, rejecting anything that would place
// the result outside `root`: absolute paths, ".." components, Windows
// drive-letter/UNC prefixes, and symlink escapes once resolved. Returns
// nullopt instead of throwing — unsafe input here is an expected,
// recoverable condition (bad CLI or repository-content input), not an
// internal invariant violation.
std::optional<std::filesystem::path> resolve_within_root(
    const std::filesystem::path& root,
    std::string_view relative);

} // namespace forge::storage

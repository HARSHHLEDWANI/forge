#include "storage/safe_path.hpp"

#include <algorithm>

namespace forge::storage {

namespace {

bool has_unsafe_component(const std::filesystem::path& relative) {
    for (const auto& part : relative) {
        if (part == "..") {
            return true;
        }
        if (part.empty()) {
            return true; // stray separators, e.g. "a//b"
        }
    }
    return false;
}

} // namespace

std::optional<std::filesystem::path> resolve_within_root(
    const std::filesystem::path& root,
    std::string_view relative) {
    if (relative.empty()) {
        return std::nullopt;
    }

    const std::filesystem::path relative_path(relative);
    if (relative_path.is_absolute()) {
        return std::nullopt;
    }
    if (relative_path.has_root_name()) {
        // Rejects Windows drive-letter ("C:foo") and UNC prefixes even when
        // the path is not is_absolute().
        return std::nullopt;
    }
    if (has_unsafe_component(relative_path)) {
        return std::nullopt;
    }

    std::error_code ec;
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        return std::nullopt;
    }
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(root / relative_path, ec);
    if (ec) {
        return std::nullopt;
    }

    // Component-wise (not string) prefix check: "/root2" must not be
    // accepted as being "inside" "/root".
    const auto mismatch_result = std::mismatch(
        canonical_root.begin(), canonical_root.end(), candidate.begin(), candidate.end());
    if (mismatch_result.first != canonical_root.end()) {
        return std::nullopt;
    }

    return candidate;
}

} // namespace forge::storage

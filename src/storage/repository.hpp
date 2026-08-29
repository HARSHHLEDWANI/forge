#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace forge::storage {

inline constexpr std::string_view kForgeDirName = ".forge";
inline constexpr std::string_view kConfigFileName = "config";

struct InitResult {
    std::filesystem::path forge_dir;
    bool reinitialized;
};

// Walks upward from `start` looking for a `.forge` directory, the same way
// Git discovers a repository from any working-directory depth. Returns the
// directory containing `.forge`, or nullopt if none is found before
// reaching the filesystem root.
std::optional<std::filesystem::path> discover_repository_root(
    const std::filesystem::path& start = std::filesystem::current_path());

// Creates `.forge` (and its config file, if not already present) under
// `target_dir`, creating `target_dir` itself if necessary. Safe to call on
// an already-initialized repository: existing config is left untouched.
InitResult initialize_repository(const std::filesystem::path& target_dir);

} // namespace forge::storage

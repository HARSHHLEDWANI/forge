#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace forge::storage {

inline constexpr std::string_view kForgeDirName = ".forge";
inline constexpr std::string_view kConfigFileName = "config";
inline constexpr std::string_view kIndexFileName = "index";
inline constexpr std::string_view kIgnoreFileName = ".forgeignore";

struct InitResult {
    std::filesystem::path forge_dir;
    bool reinitialized;
};

struct RepositoryConfig {
    int format_version = 1;
    std::filesystem::path storage_root;
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

// Reads and parses ".forge/config". Throws core::ForgeError if the file
// is missing or doesn't declare storage_root — both mean the repository
// isn't in a state anything can safely operate against.
RepositoryConfig load_config(const std::filesystem::path& forge_dir);

} // namespace forge::storage

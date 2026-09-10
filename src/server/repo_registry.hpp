#pragma once

#include <filesystem>
#include <string_view>

namespace forge::server {

// Resolves repo names hosted by this server to their on-disk location:
// `<repos_root>/<name>/.forge`. Each hosted repo is an ordinary Forge
// repository (see storage/repository.hpp) — the server only ever
// touches its ObjectStore/RefStore, never builds or checks out a
// working tree, so there's no practical difference from a "bare"
// repository even though nothing marks it as one.
class RepoRegistry {
public:
    explicit RepoRegistry(std::filesystem::path repos_root);

    bool exists(std::string_view name) const;

    // Throws core::ForgeError if `name` is empty, contains '/' or '\\',
    // or is "." or ".." — a repo name becomes a directory name directly,
    // so this is the same path-traversal concern branching.hpp's
    // create_branch already guards its own names against.
    std::filesystem::path repo_root_for(std::string_view name) const;
    std::filesystem::path forge_dir_for(std::string_view name) const;

    // Initializes `name` as a fresh, empty repository if it doesn't
    // already exist (see initialize_repository) — the server's answer
    // to "push to create", the common hosting convention. A no-op, not
    // an error, if it already exists.
    void ensure_exists(std::string_view name);

private:
    std::filesystem::path repos_root_;
};

} // namespace forge::server

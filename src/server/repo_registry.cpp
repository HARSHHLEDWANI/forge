#include "server/repo_registry.hpp"

#include "core/error.hpp"
#include "storage/repository.hpp"

namespace forge::server {

namespace {

void validate_repo_name(std::string_view name) {
    if (name.empty() || name == "." || name == "..") {
        throw core::ForgeError("invalid repository name: " + std::string(name));
    }
    // '/' and '\\' guard the directory-name use (repos_root/<name>); the
    // rest guard the query-string use (?repo=<name> — see
    // transport::parse_query_string's no-percent-decoding contract) the
    // same way branching.cpp's validate_branch_name does for branch
    // names, so nothing here ever needs escaping on either path.
    for (const char c : name) {
        if (c == '/' || c == '\\' || c == '"' || c == '&' || c == '=' || static_cast<unsigned char>(c) < 0x20) {
            throw core::ForgeError("invalid repository name: " + std::string(name));
        }
    }
}

} // namespace

RepoRegistry::RepoRegistry(std::filesystem::path repos_root) : repos_root_(std::move(repos_root)) {}

std::filesystem::path RepoRegistry::repo_root_for(std::string_view name) const {
    validate_repo_name(name);
    return repos_root_ / name;
}

std::filesystem::path RepoRegistry::forge_dir_for(std::string_view name) const {
    return repo_root_for(name) / storage::kForgeDirName;
}

bool RepoRegistry::exists(std::string_view name) const {
    return std::filesystem::is_directory(forge_dir_for(name));
}

void RepoRegistry::ensure_exists(std::string_view name) {
    if (exists(name)) {
        return;
    }
    storage::initialize_repository(repo_root_for(name));
}

} // namespace forge::server

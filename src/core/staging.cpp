#include "core/staging.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include "core/blob.hpp"
#include "core/error.hpp"
#include "storage/repository.hpp"

namespace forge::core {

namespace {

bool is_executable(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::perms perms = std::filesystem::status(path, ec).permissions();
    if (ec) {
        return false;
    }
    return (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
}

std::string read_file_content(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ForgeError("failed to read file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Resolves `target` (interpreted relative to the current working
// directory, like a normal CLI pathspec) to an absolute path and checks
// it falls within `repo_root`. Component-wise prefix check, same
// technique as storage::resolve_within_root — but that function validates
// a raw untrusted relative *string*; here the input is already an OS
// path that just needs a boundary check, a different enough shape that
// reusing it directly isn't a clean fit.
std::optional<std::filesystem::path> resolve_target_within_repo(
    const std::filesystem::path& repo_root, const std::filesystem::path& target) {
    std::error_code ec;
    const std::filesystem::path absolute_target =
        std::filesystem::weakly_canonical(std::filesystem::absolute(target), ec);
    if (ec) {
        return std::nullopt;
    }
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(repo_root, ec);
    if (ec) {
        return std::nullopt;
    }

    const auto mismatch_result = std::mismatch(
        canonical_root.begin(), canonical_root.end(), absolute_target.begin(), absolute_target.end());
    if (mismatch_result.first != canonical_root.end()) {
        return std::nullopt;
    }
    return absolute_target;
}

// "" for repo_root itself; POSIX-separated otherwise.
std::string repo_relative_posix(const std::filesystem::path& repo_root, const std::filesystem::path& path) {
    if (path == repo_root) {
        return "";
    }
    return std::filesystem::relative(path, repo_root).generic_string();
}

bool is_under_prefix(std::string_view path, std::string_view prefix) {
    if (prefix.empty()) {
        return true; // repo root: everything is "under" it
    }
    if (path == prefix) {
        return true;
    }
    return path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix &&
           path[prefix.size()] == '/';
}

void walk_and_stage(
    storage::ObjectStore& objects, const IgnoreRules& ignore_rules,
    const std::filesystem::path& repo_root, const std::filesystem::path& path,
    std::set<std::string>& found_paths, Index& index, AddResult& result) {
    std::error_code exists_ec;
    if (!std::filesystem::exists(std::filesystem::symlink_status(path, exists_ec)) || exists_ec) {
        return; // doesn't exist: nothing to stage from here
    }
    if (path.filename() == storage::kForgeDirName) {
        return;
    }

    const std::string repo_path = repo_relative_posix(repo_root, path);
    const bool is_symlink = std::filesystem::is_symlink(path);
    const bool is_dir = !is_symlink && std::filesystem::is_directory(path);

    if (!repo_path.empty() && ignore_rules.is_ignored(repo_path, is_dir)) {
        return;
    }

    if (is_dir) {
        for (const auto& child : std::filesystem::directory_iterator(path)) {
            walk_and_stage(objects, ignore_rules, repo_root, child.path(), found_paths, index, result);
        }
        return;
    }

    if (is_symlink) {
        const std::string link_target = std::filesystem::read_symlink(path).generic_string();
        const ObjectId blob_id = objects.put_blob(Blob{link_target});
        found_paths.insert(repo_path);
        index.upsert(IndexEntry{repo_path, EntryMode::Symlink, blob_id});
        result.staged.push_back(repo_path);
    } else if (std::filesystem::is_regular_file(path)) {
        const ObjectId blob_id = objects.put_blob(Blob{read_file_content(path)});
        const EntryMode mode = is_executable(path) ? EntryMode::ExecutableFile : EntryMode::RegularFile;
        found_paths.insert(repo_path);
        index.upsert(IndexEntry{repo_path, mode, blob_id});
        result.staged.push_back(repo_path);
    }
    // Other file types (sockets, FIFOs, device files) are silently
    // skipped: not meaningful content to stage.
}

} // namespace

AddResult stage_path(
    storage::ObjectStore& objects, storage::IndexStore& index_store, const IgnoreRules& ignore_rules,
    const std::filesystem::path& repo_root, const std::filesystem::path& target) {
    const std::optional<std::filesystem::path> absolute_target = resolve_target_within_repo(repo_root, target);
    if (!absolute_target) {
        throw ForgeError("path is outside the repository: " + target.string());
    }

    Index index = index_store.load();
    const std::string target_prefix = repo_relative_posix(repo_root, *absolute_target);

    AddResult result;
    std::set<std::string> found_paths;
    walk_and_stage(objects, ignore_rules, repo_root, *absolute_target, found_paths, index, result);

    std::vector<std::string> to_remove;
    for (const auto& entry : index.entries()) {
        if (is_under_prefix(entry.path, target_prefix) && found_paths.find(entry.path) == found_paths.end()) {
            to_remove.push_back(entry.path);
        }
    }
    for (const auto& path : to_remove) {
        index.remove(path);
        result.removed.push_back(path);
    }

    if (result.staged.empty() && result.removed.empty() && !std::filesystem::exists(*absolute_target)) {
        throw ForgeError("pathspec did not match any tracked or existing files: " + target.string());
    }

    index_store.save(index);
    return result;
}

} // namespace forge::core

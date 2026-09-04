#include "storage/ref_store.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/error.hpp"
#include "storage/atomic_file.hpp"
#include "storage/repository.hpp"
#include "storage/safe_path.hpp"

namespace forge::storage {

namespace {

std::string_view trim_trailing_newline(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

std::string read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

constexpr std::string_view kSymbolicPrefix = "ref: ";

} // namespace

RefStore::RefStore(std::filesystem::path forge_dir) : forge_dir_(std::move(forge_dir)) {}

std::filesystem::path RefStore::head_path() const {
    return forge_dir_ / kHeadFileName;
}

std::optional<std::filesystem::path> RefStore::branch_path(std::string_view branch_name) const {
    return resolve_within_root(forge_dir_ / kRefsHeadsDirName, branch_name);
}

std::optional<core::ObjectId> RefStore::read_branch(std::string_view branch_name) const {
    const std::optional<std::filesystem::path> path = branch_path(branch_name);
    if (!path || !std::filesystem::exists(*path)) {
        return std::nullopt;
    }

    const std::string content = read_whole_file(*path);
    const std::optional<core::ObjectId> id = core::ObjectId::parse(trim_trailing_newline(content));
    if (!id) {
        throw core::ForgeError("ref is corrupted: " + std::string(branch_name));
    }
    return id;
}

bool RefStore::branch_exists(std::string_view branch_name) const {
    return read_branch(branch_name).has_value();
}

void RefStore::update_branch(
    std::string_view branch_name, std::optional<core::ObjectId> expected_old, core::ObjectId new_id) {
    const std::optional<std::filesystem::path> path = branch_path(branch_name);
    if (!path) {
        throw core::ForgeError("invalid branch name: " + std::string(branch_name));
    }

    const std::optional<core::ObjectId> current = read_branch(branch_name);
    const bool matches =
        (current.has_value() && expected_old.has_value() && *current == *expected_old) ||
        (!current.has_value() && !expected_old.has_value());
    if (!matches) {
        throw core::ForgeError("ref update rejected: '" + std::string(branch_name) + "' changed concurrently");
    }

    std::filesystem::create_directories(path->parent_path());
    write_file_atomic(*path, new_id.to_hex() + "\n");
}

std::vector<std::string> RefStore::list_branches() const {
    const std::filesystem::path heads_dir = forge_dir_ / kRefsHeadsDirName;
    std::vector<std::string> names;
    if (!std::filesystem::exists(heads_dir)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(heads_dir)) {
        if (entry.is_regular_file()) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

RefStore::Head RefStore::read_head() const {
    const std::filesystem::path path = head_path();
    if (!std::filesystem::exists(path)) {
        throw core::ForgeError("HEAD not found: " + path.string());
    }

    const std::string content = read_whole_file(path);
    const std::string_view trimmed = trim_trailing_newline(content);

    if (trimmed.substr(0, kSymbolicPrefix.size()) == kSymbolicPrefix) {
        const std::string_view target = trimmed.substr(kSymbolicPrefix.size());
        const std::string_view prefix = kRefsHeadsDirName; // "refs/heads"
        if (target.substr(0, prefix.size()) != prefix || target.size() <= prefix.size() + 1 ||
            target[prefix.size()] != '/') {
            throw core::ForgeError("HEAD points outside refs/heads: " + path.string());
        }
        return Head{std::string(target.substr(prefix.size() + 1)), std::nullopt};
    }

    const std::optional<core::ObjectId> detached = core::ObjectId::parse(trimmed);
    if (!detached) {
        throw core::ForgeError("HEAD is malformed: " + path.string());
    }
    return Head{std::nullopt, detached};
}

void RefStore::set_head_branch(std::string_view branch_name) {
    write_file_atomic(
        head_path(), "ref: " + std::string(kRefsHeadsDirName) + "/" + std::string(branch_name) + "\n");
}

std::optional<core::ObjectId> RefStore::resolve_head() const {
    const Head head = read_head();
    if (head.detached_commit) {
        return head.detached_commit;
    }
    return read_branch(*head.branch);
}

} // namespace forge::storage

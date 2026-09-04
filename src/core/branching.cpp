#include "core/branching.hpp"

#include "core/error.hpp"

namespace forge::core {

namespace {

void validate_branch_name(std::string_view name) {
    if (name.empty()) {
        throw ForgeError("branch name must not be empty");
    }
    if (name == "HEAD") {
        throw ForgeError("'HEAD' is a reserved name");
    }
    if (name.find('/') != std::string_view::npos) {
        throw ForgeError("branch name must not contain '/': " + std::string(name));
    }
}

} // namespace

std::vector<BranchInfo> list_branches(const storage::RefStore& refs) {
    const storage::RefStore::Head head = refs.read_head();

    std::vector<BranchInfo> result;
    for (const std::string& name : refs.list_branches()) {
        const std::optional<ObjectId> commit_id = refs.read_branch(name);
        if (!commit_id) {
            continue; // shouldn't happen: list_branches() only names refs that exist
        }
        const bool is_current = head.branch.has_value() && *head.branch == name;
        result.push_back(BranchInfo{name, *commit_id, is_current});
    }
    return result;
}

void create_branch(storage::RefStore& refs, std::string_view name) {
    validate_branch_name(name);
    if (refs.branch_exists(name)) {
        throw ForgeError("branch already exists: " + std::string(name));
    }

    const std::optional<ObjectId> head_commit = refs.resolve_head();
    if (!head_commit) {
        throw ForgeError("cannot create branch: no commits yet");
    }

    refs.update_branch(name, std::nullopt, *head_commit);
}

} // namespace forge::core

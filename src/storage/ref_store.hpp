#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/object_id.hpp"

namespace forge::storage {

// Mutable name -> commit-id pointers, plus HEAD. Branches live at
// ".forge/refs/heads/<name>"; HEAD lives at ".forge/HEAD" as a symbolic
// pointer ("ref: refs/heads/<name>\n") — detached HEAD (a raw commit id)
// is Phase 7 (Checkout) scope, but read_head() already accounts for the
// shape so that addition won't need a format change. See architecture.md's
// RefStore bullet and ADR 0003 (optimistic, compare-and-swap ref updates).
class RefStore {
public:
    explicit RefStore(std::filesystem::path forge_dir);

    std::optional<core::ObjectId> read_branch(std::string_view branch_name) const;
    bool branch_exists(std::string_view branch_name) const;

    // Compare-and-swap (ADR 0003): throws core::ForgeError if the branch's
    // current value isn't exactly `expected_old` (nullopt means "the
    // branch must not exist yet"). A caller that reads a branch and later
    // updates it should always pass back what it read, so a concurrent
    // writer that got there first is detected rather than silently
    // overwritten (docs/design/failure-scenarios.md item 6).
    void update_branch(
        std::string_view branch_name, std::optional<core::ObjectId> expected_old, core::ObjectId new_id);

    // Branch names only (no "refs/heads/" prefix), sorted.
    std::vector<std::string> list_branches() const;

    struct Head {
        std::optional<std::string> branch;            // nullopt if detached
        std::optional<core::ObjectId> detached_commit; // set only if detached
    };

    // Throws core::ForgeError only if ".forge/HEAD" itself is missing or
    // malformed — HEAD pointing at a branch with no commits yet (an
    // unborn branch) is normal for a fresh repository, not corruption.
    Head read_head() const;
    void set_head_branch(std::string_view branch_name);
    void set_head_detached(core::ObjectId commit_id);

    // Resolves HEAD all the way down to a commit id. nullopt means an
    // unborn branch (HEAD's target branch doesn't exist yet).
    std::optional<core::ObjectId> resolve_head() const;

private:
    std::filesystem::path forge_dir_;
    std::filesystem::path head_path() const;
    std::optional<std::filesystem::path> branch_path(std::string_view branch_name) const;
};

} // namespace forge::storage

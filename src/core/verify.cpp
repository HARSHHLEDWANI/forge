#include "core/verify.hpp"

#include <unordered_set>

#include "core/commit.hpp"
#include "core/error.hpp"
#include "core/tree.hpp"

namespace forge::core {

namespace {

void walk_tree(
    const storage::ObjectStore& objects, const ObjectId& tree_id, const std::string& context,
    std::unordered_set<ObjectId>& visited, VerifyReport& report) {
    if (!visited.insert(tree_id).second) {
        return;
    }
    if (!objects.contains(tree_id)) {
        report.missing_objects.push_back(tree_id.to_hex() + " (tree referenced by " + context + ")");
        return;
    }

    std::vector<TreeEntry> entries;
    try {
        entries = objects.get_tree(tree_id).entries();
    } catch (const ForgeError&) {
        return; // corruption: already captured by the full-store scan pass
    }

    for (const TreeEntry& entry : entries) {
        if (entry.mode == EntryMode::Directory) {
            walk_tree(objects, entry.id, context, visited, report);
            continue;
        }
        if (!visited.insert(entry.id).second) {
            continue;
        }
        if (!objects.contains(entry.id)) {
            report.missing_objects.push_back(entry.id.to_hex() + " (blob referenced by " + context + ")");
        }
    }
}

void walk_commit(
    const storage::ObjectStore& objects, const ObjectId& commit_id, const std::string& context,
    std::unordered_set<ObjectId>& visited_commits, std::unordered_set<ObjectId>& visited_objects,
    VerifyReport& report) {
    if (!visited_commits.insert(commit_id).second) {
        return;
    }
    if (!objects.contains(commit_id)) {
        report.missing_objects.push_back(commit_id.to_hex() + " (commit referenced by " + context + ")");
        return;
    }

    ObjectId tree_id{Sha256Digest{}};
    std::vector<ObjectId> parent_ids;
    try {
        const Commit commit = objects.get_commit(commit_id);
        tree_id = commit.tree_id;
        parent_ids = commit.parent_ids;
    } catch (const ForgeError&) {
        return; // corruption: already captured by the full-store scan pass
    }

    walk_tree(objects, tree_id, context, visited_objects, report);
    for (const ObjectId& parent : parent_ids) {
        walk_commit(objects, parent, context, visited_commits, visited_objects, report);
    }
}

} // namespace

VerifyReport verify_repository(const storage::ObjectStore& objects, const storage::RefStore& refs) {
    VerifyReport report;

    const storage::ObjectStoreScan scan = objects.scan();
    report.objects_checked = scan.object_ids.size();
    report.orphaned_files = scan.unexpected_files;
    for (const ObjectId& id : scan.object_ids) {
        try {
            objects.get(id);
        } catch (const ForgeError& e) {
            report.corrupt_objects.push_back(id.to_hex() + ": " + e.what());
        }
    }

    std::unordered_set<ObjectId> visited_commits;
    std::unordered_set<ObjectId> visited_objects;
    for (const std::string& branch : refs.list_branches()) {
        const std::optional<ObjectId> commit_id = refs.read_branch(branch);
        if (!commit_id) {
            continue; // shouldn't happen: list_branches only names refs that exist
        }
        walk_commit(objects, *commit_id, "branch '" + branch + "'", visited_commits, visited_objects, report);
    }

    const storage::RefStore::Head head = refs.read_head();
    if (head.detached_commit) {
        walk_commit(objects, *head.detached_commit, "detached HEAD", visited_commits, visited_objects, report);
    }

    return report;
}

} // namespace forge::core

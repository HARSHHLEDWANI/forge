#include "core/committing.hpp"

#include "core/commit.hpp"
#include "core/error.hpp"
#include "core/index.hpp"
#include "core/tree_builder.hpp"

namespace forge::core {

CommitResult create_commit(
    storage::ObjectStore& objects, storage::IndexStore& index_store, storage::RefStore& refs,
    std::string_view author, std::string_view message, std::int64_t timestamp) {
    if (author.empty()) {
        throw ForgeError("cannot commit: no author identity configured");
    }
    if (message.empty()) {
        throw ForgeError("cannot commit: message is required");
    }

    const storage::RefStore::Head head = refs.read_head();
    if (!head.branch) {
        throw ForgeError("cannot commit: HEAD is detached");
    }

    const Index index = index_store.load();
    const ObjectId tree_id = build_tree_from_index(objects, index);

    const std::optional<ObjectId> parent_id = refs.read_branch(*head.branch);
    if (parent_id) {
        const Commit parent_commit = objects.get_commit(*parent_id);
        if (parent_commit.tree_id == tree_id) {
            throw ForgeError("nothing to commit: working tree matches HEAD");
        }
    }

    std::vector<ObjectId> parents;
    if (parent_id) {
        parents.push_back(*parent_id);
    }

    const Commit commit{tree_id, std::move(parents), std::string(author), timestamp, std::string(message)};
    const ObjectId commit_id = objects.put_commit(commit);

    refs.update_branch(*head.branch, parent_id, commit_id);

    return CommitResult{commit_id, tree_id};
}

} // namespace forge::core

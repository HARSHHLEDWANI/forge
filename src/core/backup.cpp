#include "core/backup.hpp"

#include <unordered_set>

#include "core/checkout.hpp"
#include "core/commit.hpp"
#include "core/error.hpp"
#include "core/tree.hpp"
#include "storage/index_store.hpp"

namespace forge::core {

namespace {

// Copies `id` and everything it depends on from `source` into `dest`,
// skipping anything `dest` already has — by this store's own invariant
// (an object is never "known" without everything it depends on also
// being known — see core/remote.hpp's doc comment, which relies on the
// same thing), already having an id proves its whole subtree is already
// copied too, so there's nothing further to walk into.
void copy_reachable_object(
    const storage::ObjectStore& source, storage::ObjectStore& dest, const ObjectId& id,
    std::unordered_set<ObjectId>& visited, BackupResult& result) {
    if (!visited.insert(id).second) {
        return;
    }
    if (dest.contains(id)) {
        ++result.objects_already_present;
        return;
    }

    // get() re-derives `id` from the bytes actually on disk (see
    // ObjectStore::get's own doc comment) — a backup that silently
    // skipped a source object it couldn't verify would defeat the
    // entire point of having one, so this deliberately throws instead.
    const storage::StoredObject stored = source.get(id);
    dest.put(stored.type, stored.payload);
    ++result.objects_copied;

    if (stored.type == "commit") {
        const std::optional<Commit> commit = decode_commit(stored.payload);
        if (!commit) {
            throw ForgeError("source object is a malformed commit: " + id.to_hex());
        }
        copy_reachable_object(source, dest, commit->tree_id, visited, result);
        for (const ObjectId& parent : commit->parent_ids) {
            copy_reachable_object(source, dest, parent, visited, result);
        }
    } else if (stored.type == "tree") {
        const std::optional<Tree> tree = decode_tree(stored.payload);
        if (!tree) {
            throw ForgeError("source object is a malformed tree: " + id.to_hex());
        }
        for (const TreeEntry& entry : tree->entries()) {
            copy_reachable_object(source, dest, entry.id, visited, result);
        }
    }
    // blob: nothing further to walk
}

} // namespace

BackupResult create_backup(
    const storage::ObjectStore& source_objects, const storage::RefStore& source_refs,
    const std::filesystem::path& backup_dir) {
    std::filesystem::create_directories(backup_dir / "objects");
    storage::ObjectStore backup_objects(backup_dir / "objects");
    storage::RefStore backup_refs(backup_dir);

    BackupResult result{0, 0};
    std::unordered_set<ObjectId> visited;
    for (const std::string& branch : source_refs.list_branches()) {
        if (const std::optional<ObjectId> commit_id = source_refs.read_branch(branch)) {
            copy_reachable_object(source_objects, backup_objects, *commit_id, visited, result);
        }
    }
    const storage::RefStore::Head head = source_refs.read_head();
    if (head.detached_commit) {
        copy_reachable_object(source_objects, backup_objects, *head.detached_commit, visited, result);
    }

    // Every object every ref could possibly need is now copied and
    // verified — safe to publish the refs themselves.
    for (const std::string& branch : source_refs.list_branches()) {
        if (const std::optional<ObjectId> commit_id = source_refs.read_branch(branch)) {
            backup_refs.update_branch(branch, backup_refs.read_branch(branch), *commit_id);
        }
    }
    if (head.branch) {
        backup_refs.set_head_branch(*head.branch);
    } else if (head.detached_commit) {
        backup_refs.set_head_detached(*head.detached_commit);
    }

    return result;
}

VerifyReport verify_backup(const std::filesystem::path& backup_dir) {
    const storage::ObjectStore backup_objects(backup_dir / "objects");
    const storage::RefStore backup_refs(backup_dir);
    return verify_repository(backup_objects, backup_refs);
}

storage::InitResult restore_backup(const std::filesystem::path& backup_dir, const std::filesystem::path& target_dir) {
    const storage::ObjectStore backup_objects(backup_dir / "objects");
    const storage::RefStore backup_refs(backup_dir);
    // Validates the backup actually looks like one (throws
    // core::ForgeError if backup_dir/HEAD is missing/malformed) before
    // anything at `target_dir` is touched.
    const storage::RefStore::Head head = backup_refs.read_head();

    const storage::InitResult init_result = storage::initialize_repository(target_dir);
    const storage::RepositoryConfig config = storage::load_config(init_result.forge_dir);
    storage::ObjectStore target_objects(config.storage_root);
    storage::RefStore target_refs(init_result.forge_dir);
    storage::IndexStore target_index_store(init_result.forge_dir / storage::kIndexFileName);

    BackupResult copy_stats{0, 0};
    std::unordered_set<ObjectId> visited;
    for (const std::string& branch : backup_refs.list_branches()) {
        if (const std::optional<ObjectId> commit_id = backup_refs.read_branch(branch)) {
            copy_reachable_object(backup_objects, target_objects, *commit_id, visited, copy_stats);
        }
    }
    if (head.detached_commit) {
        copy_reachable_object(backup_objects, target_objects, *head.detached_commit, visited, copy_stats);
    }

    // Same ordering as core::clone() (see core/remote.cpp): check out
    // while HEAD is still unborn — no branch ref exists yet, so
    // checkout()'s own "nothing was here before" comparison is
    // trivially correct — then create every branch ref, including the
    // one HEAD names (checkout() only points HEAD at it symbolically;
    // it never creates the branch ref itself).
    if (head.branch) {
        if (const std::optional<ObjectId> head_commit = backup_refs.read_branch(*head.branch)) {
            const CheckoutTarget checkout_target{CheckoutTargetKind::Branch, *head.branch, *head_commit};
            checkout(target_objects, target_index_store, target_refs, target_dir, checkout_target);
        }
    } else if (head.detached_commit) {
        target_refs.set_head_detached(*head.detached_commit);
    }

    for (const std::string& branch : backup_refs.list_branches()) {
        if (const std::optional<ObjectId> commit_id = backup_refs.read_branch(branch)) {
            target_refs.update_branch(branch, std::nullopt, *commit_id);
        }
    }

    return init_result;
}

} // namespace forge::core

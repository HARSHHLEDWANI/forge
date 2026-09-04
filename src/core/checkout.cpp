#include "core/checkout.hpp"

#include <fstream>
#include <sstream>

#include "core/blob.hpp"
#include "core/commit.hpp"
#include "core/error.hpp"
#include "core/object_encoding.hpp"
#include "core/tree_builder.hpp"

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

ObjectId hash_blob_content(std::string_view content) {
    return ObjectId::of(encode_canonical_object("blob", content));
}

enum class DiskLeafState {
    Missing,     // nothing at this path
    NotAPlainLeaf, // a directory, or a file type that isn't a regular file/symlink
    Readable,    // a regular file or symlink, hashable exactly like an IndexEntry
};

struct DiskLeaf {
    DiskLeafState state;
    ObjectId blob_id{Sha256Digest{}};
    EntryMode mode = EntryMode::RegularFile;
};

// Reads whatever currently sits at `absolute_path` on disk, without
// touching the ObjectStore, so it can be compared against an IndexEntry's
// recorded (mode, blob_id) to detect an uncommitted local change.
DiskLeaf inspect_disk_leaf(const std::filesystem::path& absolute_path) {
    std::error_code status_ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(absolute_path, status_ec);
    if (status_ec || !std::filesystem::exists(status)) {
        return DiskLeaf{DiskLeafState::Missing};
    }
    if (std::filesystem::is_symlink(status)) {
        const std::string target = std::filesystem::read_symlink(absolute_path).generic_string();
        return DiskLeaf{DiskLeafState::Readable, hash_blob_content(target), EntryMode::Symlink};
    }
    if (std::filesystem::is_regular_file(status)) {
        const EntryMode mode = is_executable(absolute_path) ? EntryMode::ExecutableFile : EntryMode::RegularFile;
        return DiskLeaf{DiskLeafState::Readable, hash_blob_content(read_file_content(absolute_path)), mode};
    }
    return DiskLeaf{DiskLeafState::NotAPlainLeaf}; // a directory, socket, FIFO, etc.
}

// Overwrites whatever is at `absolute_path` with `entry`'s content,
// clearing any stale file/symlink/directory that happened to occupy the
// exact spot (e.g. a path that was a directory under the old tree and a
// file under the new one).
void write_leaf(const storage::ObjectStore& objects, const std::filesystem::path& absolute_path, const IndexEntry& entry) {
    std::error_code remove_ec;
    std::filesystem::remove_all(absolute_path, remove_ec);
    std::filesystem::create_directories(absolute_path.parent_path());

    const Blob content = objects.get_blob(entry.blob_id);
    if (entry.mode == EntryMode::Symlink) {
        std::filesystem::create_symlink(content.content, absolute_path);
        return;
    }

    std::ofstream out(absolute_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw ForgeError("failed to write file: " + absolute_path.string());
    }
    out.write(content.content.data(), static_cast<std::streamsize>(content.content.size()));
    out.close();

    if (entry.mode == EntryMode::ExecutableFile) {
        std::filesystem::permissions(
            absolute_path,
            std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add);
    }
}

// Removes now-empty directories on the path from `dir` up to (but not
// including) `repo_root`, stopping at the first non-empty one. Git never
// tracks empty directories, so this mirrors real Git's behavior rather
// than inventing new semantics: a directory a checkout emptied out simply
// stops existing, the same as it would have if it were never created.
void prune_empty_ancestors(const std::filesystem::path& repo_root, std::filesystem::path dir) {
    while (dir != repo_root) {
        std::error_code ec;
        if (!std::filesystem::is_empty(dir, ec) || ec) {
            return;
        }
        std::filesystem::remove(dir);
        dir = dir.parent_path();
    }
}

} // namespace

std::optional<CheckoutTarget> resolve_checkout_target(
    const storage::RefStore& refs, const storage::ObjectStore& objects, std::string_view ref_or_commit) {
    if (const std::optional<ObjectId> branch_commit = refs.read_branch(ref_or_commit)) {
        return CheckoutTarget{CheckoutTargetKind::Branch, std::string(ref_or_commit), *branch_commit};
    }

    const std::optional<ObjectId> commit_id = ObjectId::parse(ref_or_commit);
    if (!commit_id || !objects.contains(*commit_id)) {
        return std::nullopt;
    }
    try {
        objects.get_commit(*commit_id);
    } catch (const ForgeError&) {
        return std::nullopt; // exists, but isn't a commit object
    }
    return CheckoutTarget{CheckoutTargetKind::DetachedCommit, "", *commit_id};
}

CheckoutResult checkout(
    storage::ObjectStore& objects, storage::IndexStore& index_store, storage::RefStore& refs,
    const std::filesystem::path& repo_root, const CheckoutTarget& target) {
    const Commit target_commit = objects.get_commit(target.commit_id);
    const Index new_index = flatten_tree_to_index(objects, target_commit.tree_id);

    const std::optional<ObjectId> old_head_commit_id = refs.resolve_head();
    const Index old_expected = old_head_commit_id
                                    ? flatten_tree_to_index(objects, objects.get_commit(*old_head_commit_id).tree_id)
                                    : Index{};

    const Index current_index = index_store.load();
    if (current_index.encode() != old_expected.encode()) {
        throw ForgeError("cannot switch: you have staged changes not yet committed");
    }

    // Safety pass: verify every path this checkout would touch is either
    // unmodified since HEAD (safe to overwrite/remove) or, for a newly
    // created path, currently absent (nothing to clobber). Nothing on
    // disk changes until this entire pass succeeds.
    for (const IndexEntry& old_entry : old_expected.entries()) {
        const std::optional<IndexEntry> new_entry = new_index.find(old_entry.path);
        if (new_entry && new_entry->mode == old_entry.mode && new_entry->blob_id == old_entry.blob_id) {
            continue; // unaffected by this checkout
        }
        const DiskLeaf disk = inspect_disk_leaf(repo_root / old_entry.path);
        const bool matches_head = disk.state == DiskLeafState::Readable && disk.mode == old_entry.mode &&
                                   disk.blob_id == old_entry.blob_id;
        if (!matches_head) {
            throw ForgeError("cannot switch: local modifications would be overwritten: " + old_entry.path);
        }
    }
    for (const IndexEntry& new_entry : new_index.entries()) {
        if (old_expected.find(new_entry.path)) {
            continue; // already checked above
        }
        if (inspect_disk_leaf(repo_root / new_entry.path).state != DiskLeafState::Missing) {
            throw ForgeError("cannot switch: untracked file would be overwritten: " + new_entry.path);
        }
    }

    CheckoutResult result;

    for (const IndexEntry& old_entry : old_expected.entries()) {
        if (new_index.find(old_entry.path)) {
            continue;
        }
        const std::filesystem::path absolute_path = repo_root / old_entry.path;
        std::error_code remove_ec;
        std::filesystem::remove(absolute_path, remove_ec);
        prune_empty_ancestors(repo_root, absolute_path.parent_path());
        result.removed.push_back(old_entry.path);
    }

    for (const IndexEntry& new_entry : new_index.entries()) {
        const std::optional<IndexEntry> old_entry = old_expected.find(new_entry.path);
        if (old_entry && old_entry->mode == new_entry.mode && old_entry->blob_id == new_entry.blob_id) {
            continue; // untouched
        }
        write_leaf(objects, repo_root / new_entry.path, new_entry);
        result.updated.push_back(new_entry.path);
    }

    index_store.save(new_index);

    if (target.kind == CheckoutTargetKind::Branch) {
        refs.set_head_branch(target.branch_name);
    } else {
        refs.set_head_detached(target.commit_id);
    }

    return result;
}

} // namespace forge::core

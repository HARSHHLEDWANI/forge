#include "core/merge.hpp"

#include <deque>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_set>

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

bool entries_equal(const std::optional<IndexEntry>& a, const std::optional<IndexEntry>& b) {
    if (a.has_value() != b.has_value()) {
        return false;
    }
    if (!a) {
        return true;
    }
    return a->mode == b->mode && a->blob_id == b->blob_id;
}

enum class DiskLeafState {
    Missing,       // nothing at this path
    NotAPlainLeaf, // a directory, or a file type that isn't a regular file/symlink
    Readable,      // a regular file or symlink, hashable exactly like an IndexEntry
};

struct DiskLeaf {
    DiskLeafState state;
    ObjectId blob_id{Sha256Digest{}};
    EntryMode mode = EntryMode::RegularFile;
};

// Reads whatever currently sits at `absolute_path` on disk, without
// touching the ObjectStore, so it can be compared against an IndexEntry's
// recorded (mode, blob_id) to detect an uncommitted local change — same
// approach as checkout.cpp's inspect_disk_leaf.
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

void write_blob_leaf(
    const storage::ObjectStore& objects, const std::filesystem::path& absolute_path, EntryMode mode,
    const ObjectId& blob_id) {
    std::error_code remove_ec;
    std::filesystem::remove_all(absolute_path, remove_ec);
    std::filesystem::create_directories(absolute_path.parent_path());

    const Blob content = objects.get_blob(blob_id);
    if (mode == EntryMode::Symlink) {
        std::filesystem::create_symlink(content.content, absolute_path);
        return;
    }

    std::ofstream out(absolute_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw ForgeError("failed to write file: " + absolute_path.string());
    }
    out.write(content.content.data(), static_cast<std::streamsize>(content.content.size()));
    out.close();

    if (mode == EntryMode::ExecutableFile) {
        std::filesystem::permissions(
            absolute_path,
            std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add);
    }
}

void write_text_leaf(const std::filesystem::path& absolute_path, const std::string& text) {
    std::error_code remove_ec;
    std::filesystem::remove_all(absolute_path, remove_ec);
    std::filesystem::create_directories(absolute_path.parent_path());
    std::ofstream out(absolute_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw ForgeError("failed to write file: " + absolute_path.string());
    }
    out << text;
}

// Removes now-empty directories on the path from `dir` up to (but not
// including) `repo_root`, mirroring checkout.cpp's prune_empty_ancestors.
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

// Whole-file conflict markers: block-level, not a line-by-line diff3
// merge of the two sides. Simpler than hunk-aligning the two edit
// scripts against the base and always correct (it never silently
// combines two edits that might not actually be compatible) at the cost
// of flagging a whole file as conflicted even when a line-level merge
// could have resolved it automatically — acceptable for V1 (same
// "correctness before performance/refinement" tradeoff diff.hpp's
// render_unified_diff documents for its own single-hunk simplification).
std::string conflict_markers(
    const std::optional<IndexEntry>& ours, const std::optional<IndexEntry>& theirs,
    const storage::ObjectStore& objects) {
    std::ostringstream out;
    out << "<<<<<<< ours\n";
    if (ours) {
        out << objects.get_blob(ours->blob_id).content;
    }
    out << "=======\n";
    if (theirs) {
        out << objects.get_blob(theirs->blob_id).content;
    }
    out << ">>>>>>> theirs\n";
    return out.str();
}

std::optional<ObjectId> resolve_their_commit(
    const storage::RefStore& refs, const storage::ObjectStore& objects, std::string_view ref_or_commit) {
    if (const std::optional<ObjectId> branch_commit = refs.read_branch(ref_or_commit)) {
        return branch_commit;
    }
    const std::optional<ObjectId> parsed = ObjectId::parse(ref_or_commit);
    if (!parsed || !objects.contains(*parsed)) {
        return std::nullopt;
    }
    try {
        objects.get_commit(*parsed);
    } catch (const ForgeError&) {
        return std::nullopt; // exists, but isn't a commit object
    }
    return parsed;
}

} // namespace

std::optional<ObjectId> find_merge_base(const storage::ObjectStore& objects, const ObjectId& a, const ObjectId& b) {
    std::unordered_set<ObjectId> ancestors_of_a{a};
    std::deque<ObjectId> queue{a};
    while (!queue.empty()) {
        const ObjectId id = queue.front();
        queue.pop_front();
        for (const ObjectId& parent : objects.get_commit(id).parent_ids) {
            if (ancestors_of_a.insert(parent).second) {
                queue.push_back(parent);
            }
        }
    }

    std::unordered_set<ObjectId> visited{b};
    std::deque<ObjectId> pending{b};
    while (!pending.empty()) {
        const ObjectId id = pending.front();
        pending.pop_front();
        if (ancestors_of_a.count(id) != 0) {
            return id;
        }
        for (const ObjectId& parent : objects.get_commit(id).parent_ids) {
            if (visited.insert(parent).second) {
                pending.push_back(parent);
            }
        }
    }
    return std::nullopt;
}

MergeResult merge(
    storage::ObjectStore& objects, storage::IndexStore& index_store, storage::RefStore& refs,
    const std::filesystem::path& repo_root, std::string_view their_ref_or_commit, std::string_view author,
    std::string_view message, std::int64_t timestamp) {
    if (author.empty()) {
        throw ForgeError("cannot merge: no author identity configured");
    }
    if (message.empty()) {
        throw ForgeError("cannot merge: message is required");
    }

    const storage::RefStore::Head head = refs.read_head();
    if (!head.branch) {
        throw ForgeError("cannot merge: HEAD is detached");
    }

    const std::optional<ObjectId> their_commit_id = resolve_their_commit(refs, objects, their_ref_or_commit);
    if (!their_commit_id) {
        throw ForgeError("cannot merge: unknown revision or branch: " + std::string(their_ref_or_commit));
    }

    const std::optional<ObjectId> our_commit_id = refs.read_branch(*head.branch);

    const Index our_expected_index =
        our_commit_id ? flatten_tree_to_index(objects, objects.get_commit(*our_commit_id).tree_id) : Index{};
    const Index current_index = index_store.load();
    if (current_index.encode() != our_expected_index.encode()) {
        throw ForgeError("cannot merge: you have staged changes not yet committed");
    }

    if (our_commit_id && *our_commit_id == *their_commit_id) {
        return MergeResult{};
    }

    const std::optional<ObjectId> base =
        our_commit_id ? find_merge_base(objects, *our_commit_id, *their_commit_id) : std::nullopt;
    if (our_commit_id && base && *base == *their_commit_id) {
        return MergeResult{}; // their tip is already an ancestor of ours
    }
    if (our_commit_id && !base) {
        throw ForgeError("cannot merge: refusing to merge unrelated histories");
    }

    const Index their_index = flatten_tree_to_index(objects, objects.get_commit(*their_commit_id).tree_id);
    const bool fast_forward = !our_commit_id || *base == *our_commit_id;

    if (fast_forward) {
        for (const IndexEntry& old_entry : our_expected_index.entries()) {
            const std::optional<IndexEntry> new_entry = their_index.find(old_entry.path);
            if (entries_equal(old_entry, new_entry)) {
                continue;
            }
            const DiskLeaf disk = inspect_disk_leaf(repo_root / old_entry.path);
            const bool matches_head =
                disk.state == DiskLeafState::Readable && disk.mode == old_entry.mode && disk.blob_id == old_entry.blob_id;
            if (!matches_head) {
                throw ForgeError("cannot merge: local modifications would be overwritten: " + old_entry.path);
            }
        }
        for (const IndexEntry& new_entry : their_index.entries()) {
            if (our_expected_index.find(new_entry.path)) {
                continue; // already checked above
            }
            if (inspect_disk_leaf(repo_root / new_entry.path).state != DiskLeafState::Missing) {
                throw ForgeError("cannot merge: untracked file would be overwritten: " + new_entry.path);
            }
        }

        MergeResult result;
        result.outcome = MergeOutcome::FastForward;
        for (const IndexEntry& old_entry : our_expected_index.entries()) {
            if (their_index.find(old_entry.path)) {
                continue;
            }
            const std::filesystem::path absolute_path = repo_root / old_entry.path;
            std::error_code remove_ec;
            std::filesystem::remove(absolute_path, remove_ec);
            prune_empty_ancestors(repo_root, absolute_path.parent_path());
            result.removed.push_back(old_entry.path);
        }
        for (const IndexEntry& new_entry : their_index.entries()) {
            const std::optional<IndexEntry> old_entry = our_expected_index.find(new_entry.path);
            if (old_entry && entries_equal(old_entry, new_entry)) {
                continue;
            }
            write_blob_leaf(objects, repo_root / new_entry.path, new_entry.mode, new_entry.blob_id);
            result.updated.push_back(new_entry.path);
        }
        index_store.save(their_index);
        refs.update_branch(*head.branch, our_commit_id, *their_commit_id);
        result.commit_id = their_commit_id;
        return result;
    }

    // True three-way merge: compare each side against the merge base,
    // per path.
    const Index base_index = flatten_tree_to_index(objects, objects.get_commit(*base).tree_id);

    std::set<std::string> all_paths;
    for (const IndexEntry& e : base_index.entries()) {
        all_paths.insert(e.path);
    }
    for (const IndexEntry& e : our_expected_index.entries()) {
        all_paths.insert(e.path);
    }
    for (const IndexEntry& e : their_index.entries()) {
        all_paths.insert(e.path);
    }

    struct Resolution {
        std::string path;
        bool conflict = false;
        std::optional<IndexEntry> ours_entry;
        std::optional<IndexEntry> theirs_entry;
        std::optional<IndexEntry> resolved; // nullopt = deleted; meaningful only when !conflict
    };
    std::vector<Resolution> resolutions;
    resolutions.reserve(all_paths.size());

    for (const std::string& path : all_paths) {
        const std::optional<IndexEntry> base_entry = base_index.find(path);
        const std::optional<IndexEntry> ours_entry = our_expected_index.find(path);
        const std::optional<IndexEntry> theirs_entry = their_index.find(path);

        const bool ours_changed = !entries_equal(ours_entry, base_entry);
        const bool theirs_changed = !entries_equal(theirs_entry, base_entry);

        Resolution resolution;
        resolution.path = path;
        resolution.ours_entry = ours_entry;
        resolution.theirs_entry = theirs_entry;

        if (!ours_changed && !theirs_changed) {
            resolution.resolved = base_entry;
        } else if (ours_changed && !theirs_changed) {
            resolution.resolved = ours_entry;
        } else if (!ours_changed) {
            resolution.resolved = theirs_entry;
        } else if (entries_equal(ours_entry, theirs_entry)) {
            resolution.resolved = ours_entry; // both sides made the identical change
        } else {
            resolution.conflict = true;
        }
        resolutions.push_back(std::move(resolution));
    }

    std::vector<MergeConflict> conflicts;
    for (const Resolution& resolution : resolutions) {
        if (!resolution.conflict) {
            continue;
        }
        const std::optional<IndexEntry> base_entry = base_index.find(resolution.path);
        conflicts.push_back(MergeConflict{
            resolution.path, base_entry ? std::optional(base_entry->blob_id) : std::nullopt,
            resolution.ours_entry ? std::optional(resolution.ours_entry->blob_id) : std::nullopt,
            resolution.theirs_entry ? std::optional(resolution.theirs_entry->blob_id) : std::nullopt});
    }

    // Safety pass: every path this merge would actually write to must be
    // unmodified locally (matches the current index's "ours" entry) —
    // nothing on disk changes until this whole pass succeeds, same
    // policy as checkout's/fast-forward's above.
    for (const Resolution& resolution : resolutions) {
        const bool would_write = resolution.conflict || !entries_equal(resolution.resolved, resolution.ours_entry);
        if (!would_write) {
            continue;
        }
        const DiskLeaf disk = inspect_disk_leaf(repo_root / resolution.path);
        const bool matches_ours = resolution.ours_entry
                                       ? (disk.state == DiskLeafState::Readable &&
                                          disk.mode == resolution.ours_entry->mode &&
                                          disk.blob_id == resolution.ours_entry->blob_id)
                                       : disk.state == DiskLeafState::Missing;
        if (!matches_ours) {
            throw ForgeError("cannot merge: local modifications would be overwritten: " + resolution.path);
        }
    }

    MergeResult result;
    for (const Resolution& resolution : resolutions) {
        if (resolution.conflict) {
            write_text_leaf(
                repo_root / resolution.path, conflict_markers(resolution.ours_entry, resolution.theirs_entry, objects));
            result.updated.push_back(resolution.path);
            continue;
        }
        if (entries_equal(resolution.resolved, resolution.ours_entry)) {
            continue; // already correct on disk
        }
        if (!resolution.resolved) {
            const std::filesystem::path absolute_path = repo_root / resolution.path;
            std::error_code remove_ec;
            std::filesystem::remove(absolute_path, remove_ec);
            prune_empty_ancestors(repo_root, absolute_path.parent_path());
            result.removed.push_back(resolution.path);
        } else {
            write_blob_leaf(objects, repo_root / resolution.path, resolution.resolved->mode, resolution.resolved->blob_id);
            result.updated.push_back(resolution.path);
        }
    }

    if (!conflicts.empty()) {
        result.outcome = MergeOutcome::Conflict;
        result.conflicts = std::move(conflicts);
        return result;
    }

    Index merged_index;
    for (const Resolution& resolution : resolutions) {
        if (resolution.resolved) {
            merged_index.upsert(*resolution.resolved);
        }
    }
    index_store.save(merged_index);

    const ObjectId tree_id = build_tree_from_index(objects, merged_index);
    std::vector<ObjectId> parents{*our_commit_id, *their_commit_id};
    const Commit commit{tree_id, std::move(parents), std::string(author), timestamp, std::string(message)};
    const ObjectId commit_id = objects.put_commit(commit);
    refs.update_branch(*head.branch, our_commit_id, commit_id);

    result.outcome = MergeOutcome::Merged;
    result.commit_id = commit_id;
    return result;
}

BareMergeResult merge_in_object_store(
    storage::ObjectStore& objects, storage::RefStore& refs, const ObjectId& source_commit,
    std::string_view target_branch, std::string_view author, std::string_view message, std::int64_t timestamp) {
    if (author.empty()) {
        throw ForgeError("cannot merge: no author identity configured");
    }
    if (message.empty()) {
        throw ForgeError("cannot merge: message is required");
    }

    const std::optional<ObjectId> target_commit = refs.read_branch(target_branch);
    if (!target_commit) {
        throw ForgeError("cannot merge: no such branch: " + std::string(target_branch));
    }
    if (*target_commit == source_commit) {
        return BareMergeResult{};
    }

    const std::optional<ObjectId> base = find_merge_base(objects, *target_commit, source_commit);
    if (!base) {
        throw ForgeError("cannot merge: refusing to merge unrelated histories");
    }
    if (*base == source_commit) {
        return BareMergeResult{}; // source is already an ancestor of target: nothing to do
    }

    if (*base == *target_commit) {
        // Fast-forward: no merge commit, just advance the ref.
        BareMergeResult result;
        result.outcome = MergeOutcome::FastForward;
        refs.update_branch(target_branch, target_commit, source_commit);
        result.commit_id = source_commit;
        return result;
    }

    // True three-way merge, entirely in the object store: flatten_tree_to_index/
    // build_tree_from_index (tree_builder.hpp) never touch the filesystem, so
    // this is the same resolution logic merge()'s three-way branch uses, minus
    // every working-tree/index safety check and write — there's neither to
    // protect or populate here.
    const Index base_index = flatten_tree_to_index(objects, objects.get_commit(*base).tree_id);
    const Index target_index = flatten_tree_to_index(objects, objects.get_commit(*target_commit).tree_id);
    const Index source_index = flatten_tree_to_index(objects, objects.get_commit(source_commit).tree_id);

    std::set<std::string> all_paths;
    for (const IndexEntry& e : base_index.entries()) {
        all_paths.insert(e.path);
    }
    for (const IndexEntry& e : target_index.entries()) {
        all_paths.insert(e.path);
    }
    for (const IndexEntry& e : source_index.entries()) {
        all_paths.insert(e.path);
    }

    Index merged_index;
    std::vector<MergeConflict> conflicts;
    for (const std::string& path : all_paths) {
        const std::optional<IndexEntry> base_entry = base_index.find(path);
        const std::optional<IndexEntry> ours_entry = target_index.find(path);
        const std::optional<IndexEntry> theirs_entry = source_index.find(path);

        const bool ours_changed = !entries_equal(ours_entry, base_entry);
        const bool theirs_changed = !entries_equal(theirs_entry, base_entry);

        std::optional<IndexEntry> resolved;
        bool conflict = false;
        if (!ours_changed && !theirs_changed) {
            resolved = base_entry;
        } else if (ours_changed && !theirs_changed) {
            resolved = ours_entry;
        } else if (!ours_changed) {
            resolved = theirs_entry;
        } else if (entries_equal(ours_entry, theirs_entry)) {
            resolved = ours_entry;
        } else {
            conflict = true;
        }

        if (conflict) {
            conflicts.push_back(MergeConflict{
                path, base_entry ? std::optional(base_entry->blob_id) : std::nullopt,
                ours_entry ? std::optional(ours_entry->blob_id) : std::nullopt,
                theirs_entry ? std::optional(theirs_entry->blob_id) : std::nullopt});
        } else if (resolved) {
            merged_index.upsert(*resolved);
        }
    }

    BareMergeResult result;
    if (!conflicts.empty()) {
        result.outcome = MergeOutcome::Conflict;
        result.conflicts = std::move(conflicts);
        return result;
    }

    const ObjectId tree_id = build_tree_from_index(objects, merged_index);
    std::vector<ObjectId> parents{*target_commit, source_commit};
    const Commit commit{tree_id, std::move(parents), std::string(author), timestamp, std::string(message)};
    const ObjectId commit_id = objects.put_commit(commit);
    refs.update_branch(target_branch, target_commit, commit_id);

    result.outcome = MergeOutcome::Merged;
    result.commit_id = commit_id;
    return result;
}

} // namespace forge::core

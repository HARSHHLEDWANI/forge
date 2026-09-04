#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ignore_rules.hpp"
#include "core/index.hpp"
#include "core/object_id.hpp"
#include "storage/object_store.hpp"

namespace forge::core {

enum class ChangeType { Added, Modified, Deleted };

struct EntryChange {
    std::string path;
    ChangeType type;
    std::optional<EntryMode> old_mode;
    std::optional<ObjectId> old_blob_id;
    std::optional<EntryMode> new_mode;
    std::optional<ObjectId> new_blob_id;
};

// Compares two flat path->entry snapshots and reports every path that
// differs, sorted by path. Either side can be the flattened form of a
// Tree (see tree_builder.hpp), a literal Index, or a working-tree
// snapshot (below) — diff_index doesn't care which, it only compares
// (mode, blob_id) per path. What "Added" means depends on which two
// snapshots the caller passes: index-vs-tree, it's a new file staged
// for commit; index-vs-working-tree, it's an untracked file. That
// labeling is the caller's concern, not this function's.
std::vector<EntryChange> diff_index(const Index& old_state, const Index& new_state);

// A read-only snapshot of the current working directory, shaped like an
// Index: the same walk core::stage_path does (ignore rules honored,
// ".forge" excluded), but blob ids are only computed by hashing —
// nothing is written to the ObjectStore. A status/diff check has no
// business creating objects for content that might never be committed.
Index snapshot_working_tree(const IgnoreRules& ignore_rules, const std::filesystem::path& repo_root);

enum class LineOp { Context, Insert, Delete };

struct DiffLine {
    LineOp op;
    std::string text; // one line, without its trailing newline
};

struct BlobDiff {
    bool binary = false;        // true: content differs but is treated as opaque
    std::vector<DiffLine> lines; // empty when binary or identical
};

// Line-level diff between two blob contents via an O(N*M) LCS edit
// script — simpler and far easier to verify correct than a hand-rolled
// Myers O(ND) implementation, at the cost of quadratic time/space on
// very large files. Acceptable for V1: correctness before performance
// (AGENTS.md), and this is exactly the kind of thing Phase 19 revisits
// only once a real benchmark says it matters.
//
// If either side looks binary (contains a NUL byte in its first 8000
// bytes — the same heuristic Git uses), returns {binary=true} with no
// line detail instead of attempting to line-split it.
BlobDiff diff_blob_content(std::string_view old_content, std::string_view new_content);

// Renders `diff` as a single unified-diff hunk: "--- a/<path>",
// "+++ b/<path>", one "@@ -1,<n> +1,<m> @@" header, then every line
// prefixed '+'/'-'/' '. Deliberately simpler than Git's windowed,
// multi-hunk context trimming — the whole file is one hunk, which is
// easy to get exactly right and still shows every changed line; Git's
// compact windowing is a large-file readability optimization, not a
// correctness requirement, and can be added later if it's ever missed.
// Returns "" for a non-binary diff with no actual changes.
std::string render_unified_diff(std::string_view path, const BlobDiff& diff);

struct FileDiff {
    std::string path;
    ChangeType type;
    BlobDiff content;
};

// The `forge diff` view: unstaged changes only (working tree vs. the
// index) — untracked files are deliberately excluded, matching plain
// `git diff`. For each changed path, reads the "before" content from
// the ObjectStore (already committed/staged, so it's guaranteed to be
// there) and the "after" content straight from disk (an unstaged edit
// was never written to the store).
std::vector<FileDiff> diff_working_tree(
    const storage::ObjectStore& objects, const Index& index, const IgnoreRules& ignore_rules,
    const std::filesystem::path& repo_root);

} // namespace forge::core

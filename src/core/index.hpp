#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/object_id.hpp"
#include "core/tree.hpp"

namespace forge::core {

struct IndexEntry {
    std::string path; // repo-relative, POSIX-separated (may contain '/')
    EntryMode mode;    // Directory is never valid here; the index is flat
    ObjectId blob_id;

    friend bool operator==(const IndexEntry&, const IndexEntry&) = default;
};

// The staging area: a flat, path-keyed record of what will go into the
// next commit. Unlike Tree (which nests one directory level at a time),
// an Index entry carries a full repo-relative path directly, since it
// isn't organized as a recursive structure.
class Index {
public:
    Index() = default;
    explicit Index(std::vector<IndexEntry> entries);

    const std::vector<IndexEntry>& entries() const { return entries_; }
    std::optional<IndexEntry> find(std::string_view path) const;

    // Adds a new entry, or replaces the existing one for the same path —
    // staging is idempotent/overwriting, not append-only.
    void upsert(IndexEntry entry);

    // Removes the entry for `path`, if present. Returns whether anything
    // was removed.
    bool remove(std::string_view path);

    // Canonical payload: one "<mode> <hex-id> <path>\n" line per entry,
    // sorted by path.
    std::string encode() const;

private:
    std::vector<IndexEntry> entries_;
};

// Inverse of Index::encode(). Returns nullopt (never throws) on malformed
// input, including duplicate paths — canonical_bytes routinely comes
// straight off disk.
std::optional<Index> decode_index(std::string_view canonical_bytes);

} // namespace forge::core

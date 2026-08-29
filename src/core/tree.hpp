#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/object_id.hpp"

namespace forge::core {

// Git-style modes: distinct from raw octal ints so an invalid mode can't
// even be constructed. Forge doesn't aim for on-disk compatibility with
// real Git (no such requirement in frozen-scope.md) — these string forms
// were kept anyway because they're a well-understood, unambiguous
// convention worth reusing rather than inventing a new one.
enum class EntryMode { RegularFile, ExecutableFile, Directory, Symlink };

std::string_view to_mode_string(EntryMode mode) noexcept;
std::optional<EntryMode> parse_mode_string(std::string_view mode);

struct TreeEntry {
    std::string name; // a single path component; never contains '/'
    EntryMode mode;
    ObjectId id;

    friend bool operator==(const TreeEntry&, const TreeEntry&) = default;
};

// An immutable, ordered set of named entries. Entries are always
// canonically sorted by name in encode() regardless of construction
// order, so a tree's ObjectId depends only on its content, never on how
// it happened to be built.
class Tree {
public:
    explicit Tree(std::vector<TreeEntry> entries);

    const std::vector<TreeEntry>& entries() const { return entries_; }

    // Canonical payload: one "<mode> <hex-id> <name>\n" line per entry,
    // sorted by name.
    std::string encode() const;

private:
    std::vector<TreeEntry> entries_;
};

// Inverse of Tree::encode(). Returns nullopt (never throws) on malformed
// input, including duplicate entry names — canonical_payload routinely
// comes straight off disk.
std::optional<Tree> decode_tree(std::string_view canonical_payload);

} // namespace forge::core

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/object_id.hpp"

namespace forge::core {

// A commit: an immutable snapshot pointer plus history and metadata. See
// data-model.md. `parent_ids` is ordered (first parent matters once merge
// commits exist in Phase 9); it is empty only for a repository's very
// first commit.
struct Commit {
    ObjectId tree_id;
    std::vector<ObjectId> parent_ids;
    std::string author;     // "Name <email>", opaque to Forge itself
    std::int64_t timestamp; // seconds since the Unix epoch, UTC
    std::string message;

    friend bool operator==(const Commit&, const Commit&) = default;
};

// Canonical payload: "tree <hex>\n", then one "parent <hex>\n" per parent
// (in order), then "author <author>\n" and "timestamp <seconds>\n", then a
// blank line, then the raw message bytes. The message is always last and
// never itself parsed, so it may contain anything — including embedded
// newlines — without ambiguity.
std::string encode_commit(const Commit& commit);

// Inverse of encode_commit. Returns nullopt (never throws) on malformed
// input, including a non-numeric timestamp or an invalid parent/tree hex
// id — canonical_payload routinely comes straight off disk.
std::optional<Commit> decode_commit(std::string_view canonical_payload);

} // namespace forge::core

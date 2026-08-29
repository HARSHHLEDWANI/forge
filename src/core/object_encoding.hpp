#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace forge::core {

// Encodes an object's canonical byte representation as a typed,
// length-prefixed header followed by the raw payload:
//   "<type> <payload.size()>\0<payload>"
// The type+length header keeps objects of different kinds (e.g. a tree
// and a blob that happen to share identical raw bytes) from ever hashing
// to the same ObjectId. Binary-safe: payload may contain embedded NUL
// bytes.
std::string encode_canonical_object(std::string_view type, std::string_view payload);

struct DecodedObject {
    std::string type;
    std::string payload;
};

// Inverse of encode_canonical_object. Returns nullopt (never throws) on
// malformed input — canonical_bytes routinely comes straight off disk, so
// a structurally invalid encoding is an expected, recoverable condition
// for the caller to turn into a corruption error, not an internal
// invariant violation here.
std::optional<DecodedObject> decode_canonical_object(std::string_view canonical_bytes);

} // namespace forge::core

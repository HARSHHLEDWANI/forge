#pragma once

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

} // namespace forge::core

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace forge::core {

// Standard base64 (RFC 4648 §4), '=' padded — the alphabet SSH public
// key blobs (domain/ssh_key.hpp) are encoded in.
std::string base64_encode(std::string_view bytes);
std::optional<std::string> base64_decode(std::string_view text);

// The URL-safe, unpadded variant (RFC 4648 §5) `ssh-keygen -lf`'s
// SHA256 fingerprint format uses — kept separate from base64_encode
// rather than a shared function with padding/alphabet flags, since a
// flag no caller in this codebase ever varies is just an unused
// parameter waiting to be gotten wrong.
std::string base64_encode_unpadded(std::string_view bytes);

} // namespace forge::core

#include "domain/ssh_key.hpp"

#include <cstdint>

#include "core/base64.hpp"
#include "core/hashing.hpp"

namespace forge::domain {

namespace {

// Reads the first length-prefixed string out of an SSH key blob (every
// SSH public key blob's first field is its own type name, big-endian
// uint32 length then that many bytes — RFC 4253 §5.6's "string"
// encoding). Returns nullopt if the blob is too short or the declared
// length overruns it.
std::optional<std::string> read_first_ssh_string(const std::string& blob) {
    if (blob.size() < 4) {
        return std::nullopt;
    }
    const std::uint32_t length = (static_cast<std::uint32_t>(static_cast<unsigned char>(blob[0])) << 24) |
                                  (static_cast<std::uint32_t>(static_cast<unsigned char>(blob[1])) << 16) |
                                  (static_cast<std::uint32_t>(static_cast<unsigned char>(blob[2])) << 8) |
                                  static_cast<std::uint32_t>(static_cast<unsigned char>(blob[3]));
    if (length > blob.size() - 4) {
        return std::nullopt;
    }
    return blob.substr(4, length);
}

} // namespace

std::optional<SshPublicKey> parse_ssh_public_key(std::string_view line) {
    const std::size_t first_space = line.find(' ');
    if (first_space == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second_space = line.find(' ', first_space + 1);
    const std::string_view key_type = line.substr(0, first_space);
    const std::string_view base64_data = second_space == std::string_view::npos
                                              ? line.substr(first_space + 1)
                                              : line.substr(first_space + 1, second_space - first_space - 1);
    const std::string_view comment = second_space == std::string_view::npos ? "" : line.substr(second_space + 1);

    if (key_type.empty() || base64_data.empty()) {
        return std::nullopt;
    }

    const std::optional<std::string> blob = core::base64_decode(base64_data);
    if (!blob) {
        return std::nullopt;
    }
    const std::optional<std::string> embedded_type = read_first_ssh_string(*blob);
    if (!embedded_type || *embedded_type != key_type) {
        return std::nullopt;
    }

    return SshPublicKey{std::string(key_type), std::string(base64_data), std::string(comment)};
}

std::string fingerprint_ssh_public_key(const SshPublicKey& key) {
    const std::optional<std::string> blob = core::base64_decode(key.base64_data);
    // parse_ssh_public_key already validated this decodes; a key built
    // any other way (tests aside) is a programming error, not a
    // reportable condition, so this trusts its precondition rather than
    // adding a redundant runtime check nothing else in this file needs.
    const core::Sha256Digest digest = core::sha256(*blob);
    return "SHA256:" + core::base64_encode_unpadded(std::string(
                            reinterpret_cast<const char*>(digest.data()), digest.size()));
}

} // namespace forge::domain

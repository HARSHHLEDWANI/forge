#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace forge::domain {

// A parsed `authorized_keys`-style public key line. Stored and
// fingerprinted for identity purposes only — this project doesn't (and
// doesn't need to, for RBAC/identity to work) implement the SSH
// transport protocol itself (key exchange, channel multiplexing, ...);
// that's a substantially larger undertaking than "users can register an
// SSH key" calls for, and nothing elsewhere in this codebase speaks SSH
// on the wire. See docs/adr/0004.
struct SshPublicKey {
    std::string key_type; // "ssh-ed25519", "ssh-rsa", "ecdsa-sha2-nistp256", ...
    std::string base64_data;
    std::string comment; // may be empty

    friend bool operator==(const SshPublicKey&, const SshPublicKey&) = default;
};

// Parses "<type> <base64> [comment]" — the standard authorized_keys
// line format. Validates that `base64_data` actually decodes as base64
// and that the key blob's own embedded type field (SSH public keys are
// self-describing: the first length-prefixed string inside the blob
// names the key type again) matches the outer `type` token — the same
// cross-check OpenSSH's own parser makes, catching a mismatched or
// truncated key rather than accepting it and failing confusingly later.
// Returns nullopt for anything that doesn't pass all of that.
std::optional<SshPublicKey> parse_ssh_public_key(std::string_view line);

// SHA-256 fingerprint of the raw (decoded) key blob, unpadded
// base64-encoded and prefixed "SHA256:" — exactly the format
// `ssh-keygen -lf` prints, so a user can recognize their own key at a
// glance.
std::string fingerprint_ssh_public_key(const SshPublicKey& key);

} // namespace forge::domain

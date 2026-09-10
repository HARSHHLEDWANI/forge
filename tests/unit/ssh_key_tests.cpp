#include <cstdint>

#include "core/base64.hpp"
#include "domain/ssh_key.hpp"
#include "support/test_framework.hpp"

using forge::domain::fingerprint_ssh_public_key;
using forge::domain::parse_ssh_public_key;
using forge::domain::SshPublicKey;

namespace {

std::string ssh_string(const std::string& s) {
    std::string out;
    const std::uint32_t len = static_cast<std::uint32_t>(s.size());
    out.push_back(static_cast<char>((len >> 24) & 0xFF));
    out.push_back(static_cast<char>((len >> 16) & 0xFF));
    out.push_back(static_cast<char>((len >> 8) & 0xFF));
    out.push_back(static_cast<char>(len & 0xFF));
    out += s;
    return out;
}

// A structurally valid ssh-ed25519 authorized_keys line: real SSH
// public key blobs are a length-prefixed type string followed by
// type-specific fields (for ed25519, one more length-prefixed 32-byte
// field) — the actual key material doesn't need to be a real key pair
// for parsing/fingerprinting to be exercised correctly, only
// well-formed.
std::string make_key_line(std::string_view comment = "user@host") {
    const std::string blob = ssh_string("ssh-ed25519") + ssh_string(std::string(32, 'k'));
    std::string line = "ssh-ed25519 " + forge::core::base64_encode(blob);
    if (!comment.empty()) {
        line += " " + std::string(comment);
    }
    return line;
}

} // namespace

FORGE_TEST_CASE(parse_ssh_public_key_reads_type_data_and_comment) {
    const auto key = parse_ssh_public_key(make_key_line("someone@example.com"));
    FORGE_CHECK(key.has_value());
    FORGE_CHECK(key->key_type == "ssh-ed25519");
    FORGE_CHECK(key->comment == "someone@example.com");
}

FORGE_TEST_CASE(parse_ssh_public_key_accepts_a_missing_comment) {
    const auto key = parse_ssh_public_key(make_key_line(""));
    FORGE_CHECK(key.has_value());
    FORGE_CHECK(key->comment.empty());
}

FORGE_TEST_CASE(parse_ssh_public_key_rejects_a_line_with_no_data_field) {
    FORGE_CHECK(!parse_ssh_public_key("ssh-ed25519").has_value());
}

FORGE_TEST_CASE(parse_ssh_public_key_rejects_invalid_base64) {
    FORGE_CHECK(!parse_ssh_public_key("ssh-ed25519 not!valid!base64 comment").has_value());
}

FORGE_TEST_CASE(parse_ssh_public_key_rejects_a_mismatched_embedded_type) {
    // The outer token claims ssh-rsa, but the blob's own embedded type
    // field still says ssh-ed25519 — exactly the forged/corrupted-key
    // signature the cross-check exists to catch.
    const std::string blob = ssh_string("ssh-ed25519") + ssh_string(std::string(32, 'k'));
    const std::string line = "ssh-rsa " + forge::core::base64_encode(blob) + " comment";
    FORGE_CHECK(!parse_ssh_public_key(line).has_value());
}

FORGE_TEST_CASE(fingerprint_ssh_public_key_is_deterministic_and_starts_with_sha256_prefix) {
    const auto key = parse_ssh_public_key(make_key_line());
    FORGE_CHECK(key.has_value());
    const std::string fp1 = fingerprint_ssh_public_key(*key);
    const std::string fp2 = fingerprint_ssh_public_key(*key);
    FORGE_CHECK(fp1 == fp2);
    FORGE_CHECK(fp1.rfind("SHA256:", 0) == 0);
}

FORGE_TEST_CASE(fingerprint_ssh_public_key_differs_for_different_keys) {
    const auto key_a = parse_ssh_public_key(make_key_line());
    const std::string blob_b = ssh_string("ssh-ed25519") + ssh_string(std::string(32, 'x'));
    const auto key_b = parse_ssh_public_key("ssh-ed25519 " + forge::core::base64_encode(blob_b) + " other");
    FORGE_CHECK(key_a.has_value() && key_b.has_value());
    FORGE_CHECK(fingerprint_ssh_public_key(*key_a) != fingerprint_ssh_public_key(*key_b));
}

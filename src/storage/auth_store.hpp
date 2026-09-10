#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domain/audit_log.hpp"
#include "domain/password.hpp"
#include "domain/permissions.hpp"
#include "domain/ssh_key.hpp"
#include "domain/token.hpp"

namespace forge::storage {

struct UserRecord {
    std::string username;
    domain::PasswordHash password;
    std::int64_t created_at;
};

// File-based persistence for Phase 14's auth primitives — users, their
// SSH keys, issued tokens, and per-repo role grants — plus the audit
// log those mutations get recorded to. One flat, atomically-rewritten
// text file per record kind (users.txt, ssh_keys.txt, tokens.txt,
// permissions.txt — same canonical "plain text, atomic whole-file
// rewrite" convention as storage/index_store.hpp), not a database:
// Phase 15 (PostgreSQL) is where this data actually gets one, once
// there's enough of it that whole-file rewrites stop being the right
// tradeoff. See docs/adr/0004.
class AuthStore {
public:
    explicit AuthStore(std::filesystem::path data_root);

    // Throws core::ForgeError if `username` is invalid (empty, or
    // containing a character branching.hpp's validate_branch_name would
    // also reject — same wire-protocol-safety reasoning, since usernames
    // flow through the same HTTP query strings/JSON bodies) or already
    // registered.
    void create_user(const std::string& username, std::string_view password, std::int64_t now);
    std::optional<UserRecord> find_user(std::string_view username) const;
    bool verify_user_password(std::string_view username, std::string_view password) const;

    void add_ssh_key(std::string_view username, const domain::SshPublicKey& key);
    std::vector<domain::SshPublicKey> list_ssh_keys(std::string_view username) const;

    // Issues and persists a token, returning the plaintext bearer
    // credential ("<id>.<secret>" — see domain/token.hpp) the caller
    // must hand back to the user exactly once; it can never be
    // recovered afterward.
    std::string issue_token(
        std::string_view username, domain::TokenKind kind, std::optional<std::int64_t> ttl_seconds, std::int64_t now);
    // Verifies a presented "<id>.<secret>" bearer credential, returning
    // the authenticated username on success.
    std::optional<std::string> authenticate_token(std::string_view presented, std::int64_t now) const;
    void revoke_token(std::string_view id);

    void set_permission(std::string_view repo_name, std::string_view username, domain::Role role, std::int64_t now);
    std::optional<domain::Role> get_permission(std::string_view repo_name, std::string_view username) const;
    bool repo_has_any_permission(std::string_view repo_name) const;

    domain::AuditLog& audit_log();

private:
    std::filesystem::path data_root_;
    domain::AuditLog audit_log_;

    std::filesystem::path users_path() const;
    std::filesystem::path ssh_keys_path() const;
    std::filesystem::path tokens_path() const;
    std::filesystem::path permissions_path() const;

    std::vector<UserRecord> load_users() const;
    void save_users(const std::vector<UserRecord>& users) const;

    std::vector<std::pair<std::string, domain::SshPublicKey>> load_ssh_keys() const;
    void save_ssh_keys(const std::vector<std::pair<std::string, domain::SshPublicKey>>& keys) const;

    std::vector<domain::StoredToken> load_tokens() const;
    void save_tokens(const std::vector<domain::StoredToken>& tokens) const;

    struct PermissionEntry {
        std::string repo_name;
        std::string username;
        domain::Role role;
    };
    std::vector<PermissionEntry> load_permissions() const;
    void save_permissions(const std::vector<PermissionEntry>& permissions) const;
};

} // namespace forge::storage

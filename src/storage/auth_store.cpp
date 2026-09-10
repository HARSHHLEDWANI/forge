#include "storage/auth_store.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "core/error.hpp"
#include "storage/atomic_file.hpp"

namespace forge::storage {

namespace {

// Same wire-protocol-safety policy as core/branching.cpp's
// validate_branch_name: usernames flow through this store's own
// space-delimited line format as well as the HTTP layer's query
// strings/JSON bodies once Phase 14's routes are wired up, so nothing
// that would need escaping on any of those paths is accepted here.
void validate_identifier(std::string_view name, std::string_view what) {
    if (name.empty()) {
        throw core::ForgeError(std::string(what) + " must not be empty");
    }
    for (const char c : name) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '"' || c == '\\' || c == '&' || c == '=' ||
            static_cast<unsigned char>(c) < 0x20) {
            throw core::ForgeError(std::string(what) + " contains an unsupported character: " + std::string(name));
        }
    }
}

// Splits `line` into exactly `field_count` whitespace-separated fields,
// where the last field is the remainder of the line (so it may itself
// contain spaces — e.g. an SSH key's comment) rather than just the next
// token. Returns an empty vector if there are fewer than `field_count`
// tokens available.
std::vector<std::string_view> split_fields(std::string_view line, int field_count) {
    std::vector<std::string_view> fields;
    std::size_t pos = 0;
    for (int i = 0; i < field_count - 1; ++i) {
        const std::size_t space = line.find(' ', pos);
        if (space == std::string_view::npos) {
            return {};
        }
        fields.push_back(line.substr(pos, space - pos));
        pos = space + 1;
    }
    fields.push_back(line.substr(pos));
    return fields;
}

} // namespace

AuthStore::AuthStore(std::filesystem::path data_root)
    : data_root_(std::move(data_root)), audit_log_(data_root_ / "audit.log") {}

std::filesystem::path AuthStore::users_path() const { return data_root_ / "users.txt"; }
std::filesystem::path AuthStore::ssh_keys_path() const { return data_root_ / "ssh_keys.txt"; }
std::filesystem::path AuthStore::tokens_path() const { return data_root_ / "tokens.txt"; }
std::filesystem::path AuthStore::permissions_path() const { return data_root_ / "permissions.txt"; }

domain::AuditLog& AuthStore::audit_log() { return audit_log_; }

std::vector<UserRecord> AuthStore::load_users() const {
    std::vector<UserRecord> users;
    std::ifstream in(users_path(), std::ios::binary);
    if (!in) {
        return users; // no file yet: no users, not corruption (same convention as IndexStore::load)
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        UserRecord record;
        if (!(iss >> record.username >> record.password.algorithm >> record.password.iterations >>
              record.password.salt_hex >> record.password.hash_hex >> record.created_at)) {
            throw core::ForgeError("corrupted users file: " + users_path().string());
        }
        users.push_back(std::move(record));
    }
    return users;
}

void AuthStore::save_users(const std::vector<UserRecord>& users) const {
    std::filesystem::create_directories(data_root_);
    std::ostringstream out;
    for (const UserRecord& user : users) {
        out << user.username << ' ' << user.password.algorithm << ' ' << user.password.iterations << ' '
            << user.password.salt_hex << ' ' << user.password.hash_hex << ' ' << user.created_at << '\n';
    }
    write_file_atomic(users_path(), out.str());
}

void AuthStore::create_user(const std::string& username, std::string_view password, std::int64_t now) {
    validate_identifier(username, "username");

    std::vector<UserRecord> users = load_users();
    if (std::any_of(users.begin(), users.end(), [&](const UserRecord& u) { return u.username == username; })) {
        throw core::ForgeError("user already exists: " + username);
    }

    UserRecord record;
    record.username = username;
    record.password = domain::hash_password(password);
    record.created_at = now;
    users.push_back(std::move(record));
    save_users(users);

    audit_log_.record(now, username, "user_created", "");
}

std::optional<UserRecord> AuthStore::find_user(std::string_view username) const {
    for (UserRecord& user : load_users()) {
        if (user.username == username) {
            return user;
        }
    }
    return std::nullopt;
}

bool AuthStore::verify_user_password(std::string_view username, std::string_view password) const {
    const std::optional<UserRecord> user = find_user(username);
    return user && domain::verify_password(password, user->password);
}

std::vector<std::pair<std::string, domain::SshPublicKey>> AuthStore::load_ssh_keys() const {
    std::vector<std::pair<std::string, domain::SshPublicKey>> keys;
    std::ifstream in(ssh_keys_path(), std::ios::binary);
    if (!in) {
        return keys;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string_view> fields = split_fields(line, 4);
        if (fields.size() != 4) {
            throw core::ForgeError("corrupted ssh keys file: " + ssh_keys_path().string());
        }
        keys.emplace_back(
            std::string(fields[0]), domain::SshPublicKey{std::string(fields[1]), std::string(fields[2]), std::string(fields[3])});
    }
    return keys;
}

void AuthStore::save_ssh_keys(const std::vector<std::pair<std::string, domain::SshPublicKey>>& keys) const {
    std::filesystem::create_directories(data_root_);
    std::ostringstream out;
    for (const auto& [username, key] : keys) {
        out << username << ' ' << key.key_type << ' ' << key.base64_data << ' ' << key.comment << '\n';
    }
    write_file_atomic(ssh_keys_path(), out.str());
}

void AuthStore::add_ssh_key(std::string_view username, const domain::SshPublicKey& key) {
    if (!find_user(username)) {
        throw core::ForgeError("cannot add an SSH key for an unknown user: " + std::string(username));
    }
    std::vector<std::pair<std::string, domain::SshPublicKey>> keys = load_ssh_keys();
    keys.emplace_back(std::string(username), key);
    save_ssh_keys(keys);
}

std::vector<domain::SshPublicKey> AuthStore::list_ssh_keys(std::string_view username) const {
    std::vector<domain::SshPublicKey> result;
    for (const auto& [owner, key] : load_ssh_keys()) {
        if (owner == username) {
            result.push_back(key);
        }
    }
    return result;
}

std::vector<domain::StoredToken> AuthStore::load_tokens() const {
    std::vector<domain::StoredToken> tokens;
    std::ifstream in(tokens_path(), std::ios::binary);
    if (!in) {
        return tokens;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        domain::StoredToken token;
        std::string kind_text;
        std::string expires_text;
        if (!(iss >> token.id >> token.secret_hash_hex >> token.username >> kind_text >> token.created_at >>
              expires_text)) {
            throw core::ForgeError("corrupted tokens file: " + tokens_path().string());
        }
        const std::optional<domain::TokenKind> kind = domain::parse_token_kind(kind_text);
        if (!kind) {
            throw core::ForgeError("corrupted tokens file: " + tokens_path().string());
        }
        token.kind = *kind;
        if (expires_text != "-") {
            try {
                token.expires_at = std::stoll(expires_text);
            } catch (const std::exception&) {
                throw core::ForgeError("corrupted tokens file: " + tokens_path().string());
            }
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

void AuthStore::save_tokens(const std::vector<domain::StoredToken>& tokens) const {
    std::filesystem::create_directories(data_root_);
    std::ostringstream out;
    for (const domain::StoredToken& token : tokens) {
        out << token.id << ' ' << token.secret_hash_hex << ' ' << token.username << ' '
            << domain::token_kind_to_string(token.kind) << ' ' << token.created_at << ' ';
        if (token.expires_at) {
            out << *token.expires_at;
        } else {
            out << '-';
        }
        out << '\n';
    }
    write_file_atomic(tokens_path(), out.str());
}

std::string AuthStore::issue_token(
    std::string_view username, domain::TokenKind kind, std::optional<std::int64_t> ttl_seconds, std::int64_t now) {
    if (!find_user(username)) {
        throw core::ForgeError("cannot issue a token for an unknown user: " + std::string(username));
    }

    auto [secret, stored] = domain::issue_token(username, kind, ttl_seconds, now);
    std::vector<domain::StoredToken> tokens = load_tokens();
    tokens.push_back(stored);
    save_tokens(tokens);

    audit_log_.record(
        now, username, "token_issued", std::string(domain::token_kind_to_string(kind)) + " " + stored.id);
    return secret;
}

std::optional<std::string> AuthStore::authenticate_token(std::string_view presented, std::int64_t now) const {
    const std::optional<std::pair<std::string, std::string>> split = domain::split_bearer_token(presented);
    if (!split) {
        return std::nullopt;
    }
    for (const domain::StoredToken& stored : load_tokens()) {
        if (stored.id == split->first) {
            if (domain::verify_token(split->second, stored, now)) {
                return stored.username;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void AuthStore::revoke_token(std::string_view id) {
    std::vector<domain::StoredToken> tokens = load_tokens();
    tokens.erase(
        std::remove_if(tokens.begin(), tokens.end(), [&](const domain::StoredToken& t) { return t.id == id; }),
        tokens.end());
    save_tokens(tokens);
}

std::vector<AuthStore::PermissionEntry> AuthStore::load_permissions() const {
    std::vector<PermissionEntry> permissions;
    std::ifstream in(permissions_path(), std::ios::binary);
    if (!in) {
        return permissions;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        PermissionEntry entry;
        std::string role_text;
        if (!(iss >> entry.repo_name >> entry.username >> role_text)) {
            throw core::ForgeError("corrupted permissions file: " + permissions_path().string());
        }
        const std::optional<domain::Role> role = domain::parse_role(role_text);
        if (!role) {
            throw core::ForgeError("corrupted permissions file: " + permissions_path().string());
        }
        entry.role = *role;
        permissions.push_back(std::move(entry));
    }
    return permissions;
}

void AuthStore::save_permissions(const std::vector<PermissionEntry>& permissions) const {
    std::filesystem::create_directories(data_root_);
    std::ostringstream out;
    for (const PermissionEntry& entry : permissions) {
        out << entry.repo_name << ' ' << entry.username << ' ' << domain::role_to_string(entry.role) << '\n';
    }
    write_file_atomic(permissions_path(), out.str());
}

void AuthStore::set_permission(
    std::string_view repo_name, std::string_view username, domain::Role role, std::int64_t now) {
    std::vector<PermissionEntry> permissions = load_permissions();
    bool replaced = false;
    for (PermissionEntry& entry : permissions) {
        if (entry.repo_name == repo_name && entry.username == username) {
            entry.role = role;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        permissions.push_back(PermissionEntry{std::string(repo_name), std::string(username), role});
    }
    save_permissions(permissions);

    audit_log_.record(
        now, username, "permission_granted", std::string(repo_name) + " " + std::string(domain::role_to_string(role)));
}

std::optional<domain::Role> AuthStore::get_permission(std::string_view repo_name, std::string_view username) const {
    for (const PermissionEntry& entry : load_permissions()) {
        if (entry.repo_name == repo_name && entry.username == username) {
            return entry.role;
        }
    }
    return std::nullopt;
}

bool AuthStore::repo_has_any_permission(std::string_view repo_name) const {
    for (const PermissionEntry& entry : load_permissions()) {
        if (entry.repo_name == repo_name) {
            return true;
        }
    }
    return false;
}

} // namespace forge::storage

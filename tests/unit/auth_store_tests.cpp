#include "core/error.hpp"
#include "domain/ssh_key.hpp"
#include "storage/auth_store.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::domain::Role;
using forge::domain::SshPublicKey;
using forge::domain::TokenKind;
using forge::storage::AuthStore;
using forge::test::TempDir;

FORGE_TEST_CASE(create_user_and_verify_user_password_round_trip) {
    TempDir dir;
    AuthStore store(dir.path());
    store.create_user("alice", "hunter2", 1000);

    FORGE_CHECK(store.verify_user_password("alice", "hunter2"));
    FORGE_CHECK(!store.verify_user_password("alice", "wrong"));
    FORGE_CHECK(!store.verify_user_password("nobody", "hunter2"));
}

FORGE_TEST_CASE(create_user_rejects_a_duplicate_username) {
    TempDir dir;
    AuthStore store(dir.path());
    store.create_user("alice", "hunter2", 1000);

    bool threw = false;
    try {
        store.create_user("alice", "different", 1001);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(find_user_returns_nullopt_for_an_unknown_username) {
    TempDir dir;
    AuthStore store(dir.path());
    FORGE_CHECK(!store.find_user("nobody").has_value());
}

FORGE_TEST_CASE(users_persist_across_separate_auth_store_instances) {
    TempDir dir;
    { AuthStore store(dir.path()); store.create_user("alice", "hunter2", 1000); }
    AuthStore reopened(dir.path());
    FORGE_CHECK(reopened.verify_user_password("alice", "hunter2"));
}

FORGE_TEST_CASE(add_ssh_key_requires_an_existing_user) {
    TempDir dir;
    AuthStore store(dir.path());
    const SshPublicKey key{"ssh-ed25519", "AAAA", "comment"};
    bool threw = false;
    try {
        store.add_ssh_key("nobody", key);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(add_ssh_key_and_list_ssh_keys_round_trip) {
    TempDir dir;
    AuthStore store(dir.path());
    store.create_user("alice", "hunter2", 1000);
    store.add_ssh_key("alice", SshPublicKey{"ssh-ed25519", "AAAA", "laptop"});
    store.add_ssh_key("alice", SshPublicKey{"ssh-rsa", "BBBB", "desktop"});

    const auto keys = store.list_ssh_keys("alice");
    FORGE_CHECK(keys.size() == 2);
    FORGE_CHECK(store.list_ssh_keys("bob").empty());
}

FORGE_TEST_CASE(issue_token_requires_an_existing_user) {
    TempDir dir;
    AuthStore store(dir.path());
    bool threw = false;
    try {
        store.issue_token("nobody", TokenKind::Session, std::nullopt, 1000);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(issue_token_and_authenticate_token_round_trip) {
    TempDir dir;
    AuthStore store(dir.path());
    store.create_user("alice", "hunter2", 1000);
    const std::string bearer = store.issue_token("alice", TokenKind::Session, std::nullopt, 1000);

    const auto authenticated = store.authenticate_token(bearer, 1001);
    FORGE_CHECK(authenticated.has_value());
    FORGE_CHECK(*authenticated == "alice");
}

FORGE_TEST_CASE(authenticate_token_rejects_a_revoked_token) {
    TempDir dir;
    AuthStore store(dir.path());
    store.create_user("alice", "hunter2", 1000);
    const std::string bearer = store.issue_token("alice", TokenKind::Session, std::nullopt, 1000);
    const std::string id = bearer.substr(0, bearer.find('.'));

    store.revoke_token(id);
    FORGE_CHECK(!store.authenticate_token(bearer, 1001).has_value());
}

FORGE_TEST_CASE(authenticate_token_rejects_garbage) {
    TempDir dir;
    AuthStore store(dir.path());
    FORGE_CHECK(!store.authenticate_token("not-a-real-token", 1000).has_value());
}

FORGE_TEST_CASE(permissions_round_trip_and_report_absence_correctly) {
    TempDir dir;
    AuthStore store(dir.path());
    FORGE_CHECK(!store.repo_has_any_permission("demo"));
    FORGE_CHECK(!store.get_permission("demo", "alice").has_value());

    store.set_permission("demo", "alice", Role::Admin, 1000);
    FORGE_CHECK(store.repo_has_any_permission("demo"));
    FORGE_CHECK(store.get_permission("demo", "alice") == Role::Admin);
    FORGE_CHECK(!store.get_permission("demo", "bob").has_value());
}

FORGE_TEST_CASE(set_permission_overwrites_an_existing_grant_for_the_same_user) {
    TempDir dir;
    AuthStore store(dir.path());
    store.set_permission("demo", "alice", Role::Read, 1000);
    store.set_permission("demo", "alice", Role::Write, 1001);
    FORGE_CHECK(store.get_permission("demo", "alice") == Role::Write);
}

#include "storage/credential_store.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::storage::CredentialStore;
using forge::test::TempDir;

FORGE_TEST_CASE(credential_store_save_then_find_round_trips) {
    TempDir home;
    CredentialStore store(home.path());
    store.save("forge://example.com:8080", "abc123.secret");
    FORGE_CHECK(store.find("forge://example.com:8080") == "abc123.secret");
}

FORGE_TEST_CASE(credential_store_find_returns_nullopt_for_an_unknown_remote) {
    TempDir home;
    CredentialStore store(home.path());
    FORGE_CHECK(!store.find("forge://example.com:8080").has_value());
}

FORGE_TEST_CASE(credential_store_save_overwrites_an_existing_entry_for_the_same_remote) {
    TempDir home;
    CredentialStore store(home.path());
    store.save("forge://example.com:8080", "old-token");
    store.save("forge://example.com:8080", "new-token");
    FORGE_CHECK(store.find("forge://example.com:8080") == "new-token");
}

FORGE_TEST_CASE(credential_store_keeps_entries_for_different_remotes_independent) {
    TempDir home;
    CredentialStore store(home.path());
    store.save("forge://a.example.com:8080", "token-a");
    store.save("forge://b.example.com:8080", "token-b");
    FORGE_CHECK(store.find("forge://a.example.com:8080") == "token-a");
    FORGE_CHECK(store.find("forge://b.example.com:8080") == "token-b");
}

FORGE_TEST_CASE(credential_store_remove_deletes_only_the_named_remote) {
    TempDir home;
    CredentialStore store(home.path());
    store.save("forge://a.example.com:8080", "token-a");
    store.save("forge://b.example.com:8080", "token-b");
    store.remove("forge://a.example.com:8080");
    FORGE_CHECK(!store.find("forge://a.example.com:8080").has_value());
    FORGE_CHECK(store.find("forge://b.example.com:8080") == "token-b");
}

FORGE_TEST_CASE(credential_store_remove_of_an_unknown_remote_is_a_no_op) {
    TempDir home;
    CredentialStore store(home.path());
    store.save("forge://a.example.com:8080", "token-a");
    store.remove("forge://never-saved.example.com:8080");
    FORGE_CHECK(store.find("forge://a.example.com:8080") == "token-a");
}

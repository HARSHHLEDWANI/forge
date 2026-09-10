#include "domain/token.hpp"
#include "support/test_framework.hpp"

using forge::domain::issue_token;
using forge::domain::split_bearer_token;
using forge::domain::StoredToken;
using forge::domain::TokenKind;
using forge::domain::verify_token;

FORGE_TEST_CASE(issue_token_produces_a_bearer_credential_that_verifies) {
    const auto [bearer, stored] = issue_token("alice", TokenKind::Session, std::nullopt, 1000);
    const auto split = split_bearer_token(bearer);
    FORGE_CHECK(split.has_value());
    FORGE_CHECK(split->first == stored.id);
    FORGE_CHECK(verify_token(split->second, stored, 1000));
}

FORGE_TEST_CASE(verify_token_rejects_the_wrong_secret) {
    const auto [bearer, stored] = issue_token("alice", TokenKind::Session, std::nullopt, 1000);
    (void)bearer;
    FORGE_CHECK(!verify_token("wrong-secret", stored, 1000));
}

FORGE_TEST_CASE(verify_token_respects_expiry) {
    const auto [bearer, stored] = issue_token("alice", TokenKind::Session, 60, 1000);
    const auto split = split_bearer_token(bearer);
    FORGE_CHECK(verify_token(split->second, stored, 1059)); // just before expiry
    FORGE_CHECK(!verify_token(split->second, stored, 1060)); // at expiry
    FORGE_CHECK(!verify_token(split->second, stored, 2000)); // long after
}

FORGE_TEST_CASE(a_token_with_no_ttl_never_expires) {
    const auto [bearer, stored] = issue_token("alice", TokenKind::PersonalAccessToken, std::nullopt, 1000);
    const auto split = split_bearer_token(bearer);
    FORGE_CHECK(!stored.expires_at.has_value());
    FORGE_CHECK(verify_token(split->second, stored, 100000000));
}

FORGE_TEST_CASE(two_issued_tokens_never_collide) {
    const auto [bearer_a, stored_a] = issue_token("alice", TokenKind::Session, std::nullopt, 1000);
    const auto [bearer_b, stored_b] = issue_token("alice", TokenKind::Session, std::nullopt, 1000);
    FORGE_CHECK(bearer_a != bearer_b);
    FORGE_CHECK(stored_a.id != stored_b.id);
    FORGE_CHECK(stored_a.secret_hash_hex != stored_b.secret_hash_hex);
}

FORGE_TEST_CASE(split_bearer_token_rejects_a_token_with_no_dot) {
    FORGE_CHECK(!split_bearer_token("no-dot-here").has_value());
}

FORGE_TEST_CASE(split_bearer_token_rejects_an_empty_id_or_secret) {
    FORGE_CHECK(!split_bearer_token(".secret").has_value());
    FORGE_CHECK(!split_bearer_token("id.").has_value());
}

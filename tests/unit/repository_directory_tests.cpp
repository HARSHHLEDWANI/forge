#include "database/repository_directory.hpp"
#include "support/postgres_test_support.hpp"

using namespace forge::database;
using forge::test::ensure_migrations_applied;
using forge::test::PostgresFixture;

FORGE_PG_TEST_CASE(create_user_and_find_user_round_trip) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);

    const std::int64_t id = create_user(fixture.connection, "alice", "hashed-password");
    const auto by_name = find_user_by_username(fixture.connection, "alice");
    const auto by_id = find_user_by_id(fixture.connection, id);

    FORGE_CHECK(by_name.has_value() && by_name->id == id);
    FORGE_CHECK(by_id.has_value() && by_id->username == "alice");
    FORGE_CHECK(!find_user_by_username(fixture.connection, "nobody").has_value());
}

FORGE_PG_TEST_CASE(create_user_rejects_a_duplicate_username) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    create_user(fixture.connection, "alice", "hash1");

    bool threw = false;
    try {
        create_user(fixture.connection, "alice", "hash2");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_PG_TEST_CASE(create_repository_also_creates_its_metadata_row) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const std::int64_t owner_id = create_user(fixture.connection, "alice", "hash");

    const std::int64_t repo_id = create_repository(fixture.connection, "demo", owner_id);
    const auto repo = find_repository_by_name(fixture.connection, "demo");
    FORGE_CHECK(repo.has_value() && repo->owner_id == owner_id);

    const auto metadata = fixture.connection.exec(
        "SELECT default_branch, is_private FROM repository_metadata WHERE repository_id = $1",
        {std::to_string(repo_id)});
    FORGE_CHECK(metadata.rows.size() == 1);
    FORGE_CHECK(metadata.rows[0][0].value_or("") == "main");
    FORGE_CHECK(metadata.rows[0][1].value_or("") == "f");
}

FORGE_PG_TEST_CASE(create_repository_rejects_an_unknown_owner) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    bool threw = false;
    try {
        create_repository(fixture.connection, "demo", 999999999);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_PG_TEST_CASE(set_membership_upserts_a_role) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const std::int64_t owner_id = create_user(fixture.connection, "alice", "hash");
    const std::int64_t member_id = create_user(fixture.connection, "bob", "hash");
    const std::int64_t repo_id = create_repository(fixture.connection, "demo", owner_id);

    set_membership(fixture.connection, repo_id, member_id, "read");
    FORGE_CHECK(get_membership_role(fixture.connection, repo_id, member_id) == "read");

    set_membership(fixture.connection, repo_id, member_id, "write");
    FORGE_CHECK(get_membership_role(fixture.connection, repo_id, member_id) == "write");

    FORGE_CHECK(!get_membership_role(fixture.connection, repo_id, owner_id).has_value());
}

FORGE_PG_TEST_CASE(set_membership_rejects_an_invalid_role) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const std::int64_t owner_id = create_user(fixture.connection, "alice", "hash");
    const std::int64_t repo_id = create_repository(fixture.connection, "demo", owner_id);

    bool threw = false;
    try {
        set_membership(fixture.connection, repo_id, owner_id, "superuser");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_PG_TEST_CASE(list_repository_memberships_returns_every_member_sorted_by_username) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const std::int64_t owner_id = create_user(fixture.connection, "alice", "hash");
    const std::int64_t repo_id = create_repository(fixture.connection, "demo", owner_id);
    const std::int64_t bob = create_user(fixture.connection, "bob", "hash");
    const std::int64_t carol = create_user(fixture.connection, "carol", "hash");
    set_membership(fixture.connection, repo_id, carol, "admin");
    set_membership(fixture.connection, repo_id, bob, "read");

    const auto members = list_repository_memberships(fixture.connection, repo_id);
    FORGE_CHECK(members.size() == 2);
    FORGE_CHECK(members[0].username == "bob" && members[0].role == "read");
    FORGE_CHECK(members[1].username == "carol" && members[1].role == "admin");
}

FORGE_PG_TEST_CASE(enqueue_and_claim_and_finish_a_job) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const std::int64_t job_id = enqueue_job(fixture.connection, "gc", "{}");

    const auto claimed = claim_next_pending_job(fixture.connection);
    FORGE_CHECK(claimed.has_value());
    FORGE_CHECK(claimed->id == job_id);
    FORGE_CHECK(claimed->status == "running");

    finish_job(fixture.connection, job_id, true, "");
    const auto status = fixture.connection.exec("SELECT status, error FROM jobs WHERE id = $1", {std::to_string(job_id)});
    FORGE_CHECK(status.rows[0][0].value_or("") == "done");
    FORGE_CHECK(!status.rows[0][1].has_value());
}

FORGE_PG_TEST_CASE(claim_next_pending_job_returns_nullopt_when_the_queue_is_empty) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    FORGE_CHECK(!claim_next_pending_job(fixture.connection).has_value());
}

FORGE_PG_TEST_CASE(finish_job_records_the_error_on_failure) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const std::int64_t job_id = enqueue_job(fixture.connection, "backup", "{}");
    claim_next_pending_job(fixture.connection);
    finish_job(fixture.connection, job_id, false, "disk full");

    const auto status =
        fixture.connection.exec("SELECT status, error FROM jobs WHERE id = $1", {std::to_string(job_id)});
    FORGE_CHECK(status.rows[0][0].value_or("") == "failed");
    FORGE_CHECK(status.rows[0][1].value_or("") == "disk full");
}

// Two workers polling concurrently must never claim the same job — the
// whole point of `FOR UPDATE SKIP LOCKED` (see repository_directory.hpp's
// claim_next_pending_job doc comment). This needs the claiming
// connection's transaction to still be open when the second connection
// polls, so it can't use claim_next_pending_job() (which commits
// immediately) or the rolled-back PostgresFixture (a second connection
// can't see uncommitted rows from a different connection to begin
// with) — it drives both connections by hand and cleans up its own
// committed row afterward.
FORGE_PG_TEST_CASE(concurrent_workers_never_claim_the_same_job_via_skip_locked) {
    PostgresConnection setup(pg_url);
    ensure_migrations_applied(setup);
    const std::int64_t job_id = enqueue_job(setup, "concurrent-probe", "{}");

    PostgresConnection worker_a(pg_url);
    PostgresConnection worker_b(pg_url);

    worker_a.begin();
    const auto claimed_by_a = worker_a.exec(
        "SELECT id FROM jobs WHERE id = $1 AND status = 'pending' FOR UPDATE SKIP LOCKED", {std::to_string(job_id)});
    FORGE_CHECK(claimed_by_a.rows.size() == 1); // A holds the lock, uncommitted

    worker_b.begin();
    const auto claimed_by_b = worker_b.exec(
        "SELECT id FROM jobs WHERE id = $1 AND status = 'pending' FOR UPDATE SKIP LOCKED", {std::to_string(job_id)});
    FORGE_CHECK(claimed_by_b.rows.empty()); // B skips the row A is holding, rather than blocking or double-claiming

    worker_b.rollback();
    worker_a.rollback();

    setup.exec("DELETE FROM jobs WHERE id = $1", {std::to_string(job_id)});
}

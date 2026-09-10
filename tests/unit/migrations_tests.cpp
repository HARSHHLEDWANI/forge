#include "database/migrations.hpp"
#include "support/postgres_test_support.hpp"

using forge::database::apply_pending_migrations;
using forge::database::PostgresConnection;

FORGE_PG_TEST_CASE(apply_pending_migrations_is_idempotent) {
    PostgresConnection connection(pg_url);
    // Whatever ran before this test already applied everything; a
    // second call must find nothing left to do rather than re-running
    // (and failing on) a migration that already exists.
    apply_pending_migrations(connection, FORGE_MIGRATIONS_DIR);
    const std::size_t second_run = apply_pending_migrations(connection, FORGE_MIGRATIONS_DIR);
    FORGE_CHECK(second_run == 0);
}

FORGE_PG_TEST_CASE(applied_migrations_are_recorded_in_schema_migrations) {
    PostgresConnection connection(pg_url);
    apply_pending_migrations(connection, FORGE_MIGRATIONS_DIR);
    const auto result = connection.exec("SELECT count(*) FROM schema_migrations WHERE version = 1");
    FORGE_CHECK(result.rows[0][0].value_or("0") == "1");
}

FORGE_PG_TEST_CASE(migration_creates_the_expected_tables) {
    PostgresConnection connection(pg_url);
    apply_pending_migrations(connection, FORGE_MIGRATIONS_DIR);
    for (const std::string table :
         {"users", "repositories", "repository_metadata", "repository_memberships", "jobs"}) {
        const auto result = connection.exec(
            "SELECT to_regclass($1) IS NOT NULL AS exists", {"public." + table});
        FORGE_CHECK(result.rows[0][0].value_or("f") == "t");
    }
}

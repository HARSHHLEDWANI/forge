#pragma once

// Phase 15's Postgres-backed tests only run when a live database is
// reachable — see docker-compose.yml, which sets FORGE_TEST_DATABASE_URL
// for its `dev` service. The plain `docker run forge-dev` path
// scripts/dev-build.ps1 uses doesn't set it, so these tests just no-op
// there rather than failing the whole suite over an optional dependency
// nothing else in this codebase needs to build or run.
//
// Usage: `FORGE_PG_TEST_CASE(name) { ... }` behaves exactly like
// FORGE_TEST_CASE, except the body only runs if FORGE_TEST_DATABASE_URL
// is set; `pg_url` names the connection string inside the body.

#include <cstdlib>
#include <optional>
#include <string>

#include "database/migrations.hpp"
#include "database/postgres_connection.hpp"
#include "support/test_framework.hpp"

namespace forge::test {

inline std::optional<std::string> postgres_test_url() {
    const char* url = std::getenv("FORGE_TEST_DATABASE_URL");
    if (url == nullptr || *url == '\0') {
        return std::nullopt;
    }
    return std::string(url);
}

#ifndef FORGE_MIGRATIONS_DIR
#error "FORGE_MIGRATIONS_DIR must be defined (see tests/CMakeLists.txt)"
#endif

inline void ensure_migrations_applied(forge::database::PostgresConnection& connection) {
    forge::database::apply_pending_migrations(connection, FORGE_MIGRATIONS_DIR);
}

// Connects, applies any pending migrations (idempotent, and committed
// independently — see apply_pending_migrations), then opens one
// transaction that every test operation runs inside. Never commits it:
// rolling back on destruction is what keeps the shared test database
// clean between test cases and between runs, without needing a DROP/
// recreate step anywhere.
struct PostgresFixture {
    forge::database::PostgresConnection connection;
    forge::database::Transaction transaction;

    explicit PostgresFixture(const std::string& url) : connection(url), transaction(connection) {}
};

} // namespace forge::test

#define FORGE_PG_TEST_CASE(unique_name)                                                                              \
    static void unique_name##_body(const std::string& pg_url);                                                      \
    FORGE_TEST_CASE(unique_name) {                                                                                   \
        const auto url = forge::test::postgres_test_url();                                                          \
        if (!url) {                                                                                                  \
            return;                                                                                                  \
        }                                                                                                            \
        unique_name##_body(*url);                                                                                    \
    }                                                                                                                \
    static void unique_name##_body([[maybe_unused]] const std::string& pg_url)

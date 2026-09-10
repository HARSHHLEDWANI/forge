#include "core/error.hpp"
#include "database/postgres_connection.hpp"
#include "support/postgres_test_support.hpp"

using forge::database::PostgresConnection;
using forge::database::QueryResult;
using forge::database::Transaction;

FORGE_PG_TEST_CASE(exec_runs_a_simple_select) {
    PostgresConnection connection(pg_url);
    const QueryResult result = connection.exec("SELECT 1 + 1 AS sum");
    FORGE_CHECK(result.rows.size() == 1);
    FORGE_CHECK(result.rows[0][0].value_or("") == "2");
}

FORGE_PG_TEST_CASE(exec_binds_parameters_instead_of_concatenating_sql) {
    PostgresConnection connection(pg_url);
    // If this were string concatenation, the embedded quote would break
    // out of the literal; parameter binding treats it as ordinary data.
    const QueryResult result = connection.exec("SELECT $1::text AS echoed", {std::string("'; DROP TABLE users; --")});
    FORGE_CHECK(result.rows[0][0].value_or("") == "'; DROP TABLE users; --");
}

FORGE_PG_TEST_CASE(exec_represents_sql_null_as_nullopt) {
    PostgresConnection connection(pg_url);
    const QueryResult result = connection.exec("SELECT NULL::text AS n");
    FORGE_CHECK(!result.rows[0][0].has_value());
}

FORGE_PG_TEST_CASE(a_null_query_param_binds_sql_null) {
    PostgresConnection connection(pg_url);
    const QueryResult result = connection.exec("SELECT $1::text IS NULL AS is_null", {std::nullopt});
    FORGE_CHECK(result.rows[0][0].value_or("") == "t");
}

FORGE_PG_TEST_CASE(exec_throws_on_invalid_sql) {
    PostgresConnection connection(pg_url);
    bool threw = false;
    try {
        connection.exec("SELECT this is not valid sql");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_PG_TEST_CASE(postgres_connection_throws_on_a_bad_connection_string) {
    bool threw = false;
    try {
        PostgresConnection connection("postgresql://nobody:nothing@127.0.0.1:1/does-not-exist?connect_timeout=1");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_PG_TEST_CASE(transaction_rolls_back_by_default) {
    PostgresConnection connection(pg_url);
    connection.exec("CREATE TEMPORARY TABLE tx_rollback_probe (n INT)");
    {
        Transaction inner(connection);
        connection.exec("INSERT INTO tx_rollback_probe VALUES (1)");
        // no commit() — destructor rolls back
    }
    const QueryResult result = connection.exec("SELECT count(*) FROM tx_rollback_probe");
    FORGE_CHECK(result.rows[0][0].value_or("") == "0");
}

FORGE_PG_TEST_CASE(transaction_persists_changes_once_committed) {
    PostgresConnection connection(pg_url);
    connection.exec("CREATE TEMPORARY TABLE tx_commit_probe (n INT)");
    {
        Transaction inner(connection);
        connection.exec("INSERT INTO tx_commit_probe VALUES (1)");
        inner.commit();
    }
    const QueryResult result = connection.exec("SELECT count(*) FROM tx_commit_probe");
    FORGE_CHECK(result.rows[0][0].value_or("") == "1");
}

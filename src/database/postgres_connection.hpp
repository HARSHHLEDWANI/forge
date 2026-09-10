#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp" // callers catch core::ForgeError from exec()/begin()/commit()/rollback()

namespace forge::database {

// A NULL is represented as nullopt, not the empty string — SQL NULL and
// "" are different values, and collapsing them would be a real
// correctness bug the moment a nullable column matters (e.g.
// repository_metadata's optional fields, Phase 16+).
using QueryParam = std::optional<std::string>;
using QueryRow = std::vector<QueryParam>;

struct QueryResult {
    std::vector<std::string> column_names;
    std::vector<QueryRow> rows;
    // For an INSERT/UPDATE/DELETE with no RETURNING clause: how many
    // rows it touched. 0 for a SELECT (use rows.size() instead).
    long affected_rows = 0;
};

// RAII wrapper over one libpq connection. Every query goes through
// libpq's parameterized exec (PQexecParams) with parameter values
// passed separately from the SQL text — never string-concatenated —
// so this can never be SQL-injected regardless of what a caller passes
// as a parameter value (AGENTS.md's "be careful not to introduce SQL
// injection").
class PostgresConnection {
public:
    // Throws core::ForgeError if the connection can't be established —
    // `connection_string` is libpq's own URI/keyword-value form
    // ("postgresql://user:pass@host:port/dbname").
    explicit PostgresConnection(std::string_view connection_string);
    ~PostgresConnection();

    PostgresConnection(const PostgresConnection&) = delete;
    PostgresConnection& operator=(const PostgresConnection&) = delete;

    // Runs one parameterized SQL command. Throws core::ForgeError on
    // any failure (connection lost, constraint violation, syntax
    // error, ...) with libpq's own error message attached.
    QueryResult exec(std::string_view sql, const std::vector<QueryParam>& params = {});

    // Runs `sql` as-is via libpq's plain (unparameterized) exec, which
    // — unlike exec()'s PQexecParams — allows multiple ';'-separated
    // statements in one call. Only used for applying a whole migration
    // file (migrations.hpp) verbatim; every other caller uses exec()
    // so parameter values are never at risk of being interpreted as SQL.
    QueryResult exec_batch(std::string_view sql);

    // Nestable via SAVEPOINT: the first begin() issues a real BEGIN, a
    // begin() while already inside one issues SAVEPOINT instead (and
    // the matching commit()/rollback() RELEASEs or ROLLBACK TOs it) —
    // so a DAO function that opens its own Transaction (see
    // repository_directory.cpp's create_repository) still composes
    // correctly when called from inside a caller's own already-open
    // transaction (a test fixture wrapping everything in one rolled-
    // back transaction for isolation, say — see
    // tests/support/postgres_test_support.hpp) instead of the inner
    // commit() silently committing the outer transaction early, or the
    // inner BEGIN silently becoming a same-connection no-op (both real
    // failure modes without this — the whole reason "Study: ...
    // isolation, locking" names transactions as something to actually
    // understand, not just call). Throws core::ForgeError if commit()
    // or rollback() is called with no transaction open.
    void begin();
    void commit();
    void rollback();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Begins a transaction (or, nested inside another already-open one, a
// savepoint — see begin()'s doc comment) on construction; rolls it back
// on destruction unless commit() was called first — so an exception
// thrown mid-transaction (a core::ForgeError from a failed query, say)
// can't leave a half-applied transaction open on the connection.
class Transaction {
public:
    explicit Transaction(PostgresConnection& connection);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    PostgresConnection& connection_;
    bool finished_ = false;
};

} // namespace forge::database

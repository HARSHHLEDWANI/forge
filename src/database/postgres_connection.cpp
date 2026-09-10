#include "database/postgres_connection.hpp"

#include <cstdlib>
#include <vector>

#include <libpq-fe.h>

#include "core/error.hpp"

namespace forge::database {

namespace {

// PQclear must run whether exec throws or returns normally.
struct ResultGuard {
    PGresult* result;
    ~ResultGuard() { PQclear(result); }
};

QueryResult extract(PGresult* result) {
    QueryResult out;
    const int columns = PQnfields(result);
    for (int c = 0; c < columns; ++c) {
        out.column_names.emplace_back(PQfname(result, c));
    }

    const int rows = PQntuples(result);
    out.rows.reserve(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        QueryRow row;
        row.reserve(static_cast<std::size_t>(columns));
        for (int c = 0; c < columns; ++c) {
            if (PQgetisnull(result, r, c)) {
                row.emplace_back(std::nullopt);
            } else {
                row.emplace_back(std::string(PQgetvalue(result, r, c), static_cast<std::size_t>(PQgetlength(result, r, c))));
            }
        }
        out.rows.push_back(std::move(row));
    }

    if (const char* affected = PQcmdTuples(result); affected != nullptr && affected[0] != '\0') {
        out.affected_rows = std::atol(affected);
    }
    return out;
}

void check_status(PGresult* result, PGconn* conn) {
    const ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        throw core::ForgeError(std::string("postgres query failed: ") + PQerrorMessage(conn));
    }
}

} // namespace

struct PostgresConnection::Impl {
    PGconn* conn = nullptr;
    int transaction_depth = 0; // 0 = no transaction open; see begin()'s doc comment on SAVEPOINT nesting
};

PostgresConnection::PostgresConnection(std::string_view connection_string) : impl_(std::make_unique<Impl>()) {
    impl_->conn = PQconnectdb(std::string(connection_string).c_str());
    if (impl_->conn == nullptr || PQstatus(impl_->conn) != CONNECTION_OK) {
        const std::string message = impl_->conn != nullptr ? PQerrorMessage(impl_->conn) : "out of memory";
        if (impl_->conn != nullptr) {
            PQfinish(impl_->conn);
        }
        throw core::ForgeError("failed to connect to postgres: " + message);
    }
}

PostgresConnection::~PostgresConnection() {
    if (impl_ && impl_->conn != nullptr) {
        PQfinish(impl_->conn);
    }
}

QueryResult PostgresConnection::exec(std::string_view sql, const std::vector<QueryParam>& params) {
    std::vector<const char*> values;
    values.reserve(params.size());
    for (const QueryParam& param : params) {
        values.push_back(param ? param->c_str() : nullptr);
    }

    PGresult* raw = PQexecParams(
        impl_->conn, std::string(sql).c_str(), static_cast<int>(values.size()), nullptr, values.data(), nullptr,
        nullptr, 0);
    ResultGuard guard{raw};
    check_status(raw, impl_->conn);
    return extract(raw);
}

QueryResult PostgresConnection::exec_batch(std::string_view sql) {
    PGresult* raw = PQexec(impl_->conn, std::string(sql).c_str());
    ResultGuard guard{raw};
    check_status(raw, impl_->conn);
    return extract(raw);
}

void PostgresConnection::begin() {
    if (impl_->transaction_depth == 0) {
        exec("BEGIN");
    } else {
        exec("SAVEPOINT sp" + std::to_string(impl_->transaction_depth + 1));
    }
    ++impl_->transaction_depth;
}

void PostgresConnection::commit() {
    if (impl_->transaction_depth == 0) {
        throw core::ForgeError("commit() called with no transaction in progress");
    }
    if (impl_->transaction_depth == 1) {
        exec("COMMIT");
    } else {
        exec("RELEASE SAVEPOINT sp" + std::to_string(impl_->transaction_depth));
    }
    --impl_->transaction_depth;
}

void PostgresConnection::rollback() {
    if (impl_->transaction_depth == 0) {
        throw core::ForgeError("rollback() called with no transaction in progress");
    }
    if (impl_->transaction_depth == 1) {
        exec("ROLLBACK");
    } else {
        exec("ROLLBACK TO SAVEPOINT sp" + std::to_string(impl_->transaction_depth));
    }
    --impl_->transaction_depth;
}

Transaction::Transaction(PostgresConnection& connection) : connection_(connection) { connection_.begin(); }

Transaction::~Transaction() {
    if (!finished_) {
        try {
            connection_.rollback();
        } catch (const core::ForgeError&) {
            // Already-dead connection: nothing left to roll back.
        }
    }
}

void Transaction::commit() {
    connection_.commit();
    finished_ = true;
}

} // namespace forge::database

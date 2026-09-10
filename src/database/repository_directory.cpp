#include "database/repository_directory.hpp"

#include "core/error.hpp"

namespace forge::database {

namespace {

std::int64_t require_id(const QueryResult& result) {
    if (result.rows.empty() || !result.rows[0].at(0)) {
        throw core::ForgeError("expected an id back from an INSERT ... RETURNING id");
    }
    return std::stoll(*result.rows[0][0]);
}

} // namespace

std::int64_t create_user(PostgresConnection& db, std::string_view username, std::string_view password_hash) {
    const QueryResult result = db.exec(
        "INSERT INTO users (username, password_hash) VALUES ($1, $2) RETURNING id",
        {std::string(username), std::string(password_hash)});
    return require_id(result);
}

std::optional<DbUser> find_user_by_username(PostgresConnection& db, std::string_view username) {
    const QueryResult result =
        db.exec("SELECT id, username, password_hash FROM users WHERE username = $1", {std::string(username)});
    if (result.rows.empty()) {
        return std::nullopt;
    }
    const QueryRow& row = result.rows[0];
    return DbUser{std::stoll(*row[0]), *row[1], *row[2]};
}

std::optional<DbUser> find_user_by_id(PostgresConnection& db, std::int64_t id) {
    const QueryResult result =
        db.exec("SELECT id, username, password_hash FROM users WHERE id = $1", {std::to_string(id)});
    if (result.rows.empty()) {
        return std::nullopt;
    }
    const QueryRow& row = result.rows[0];
    return DbUser{std::stoll(*row[0]), *row[1], *row[2]};
}

std::int64_t create_repository(PostgresConnection& db, std::string_view name, std::int64_t owner_id) {
    Transaction transaction(db);
    const QueryResult result = db.exec(
        "INSERT INTO repositories (name, owner_id) VALUES ($1, $2) RETURNING id",
        {std::string(name), std::to_string(owner_id)});
    const std::int64_t id = require_id(result);
    db.exec("INSERT INTO repository_metadata (repository_id) VALUES ($1)", {std::to_string(id)});
    transaction.commit();
    return id;
}

std::vector<DbRepository> list_repositories(PostgresConnection& db) {
    const QueryResult result = db.exec("SELECT id, name, owner_id FROM repositories ORDER BY name");
    std::vector<DbRepository> repositories;
    repositories.reserve(result.rows.size());
    for (const QueryRow& row : result.rows) {
        repositories.push_back(DbRepository{std::stoll(*row[0]), *row[1], std::stoll(*row[2])});
    }
    return repositories;
}

std::optional<DbRepository> find_repository_by_name(PostgresConnection& db, std::string_view name) {
    const QueryResult result =
        db.exec("SELECT id, name, owner_id FROM repositories WHERE name = $1", {std::string(name)});
    if (result.rows.empty()) {
        return std::nullopt;
    }
    const QueryRow& row = result.rows[0];
    return DbRepository{std::stoll(*row[0]), *row[1], std::stoll(*row[2])};
}

void set_membership(
    PostgresConnection& db, std::int64_t repository_id, std::int64_t user_id, std::string_view role) {
    db.exec(
        "INSERT INTO repository_memberships (repository_id, user_id, role) VALUES ($1, $2, $3) "
        "ON CONFLICT (repository_id, user_id) DO UPDATE SET role = EXCLUDED.role",
        {std::to_string(repository_id), std::to_string(user_id), std::string(role)});
}

std::optional<std::string> get_membership_role(
    PostgresConnection& db, std::int64_t repository_id, std::int64_t user_id) {
    const QueryResult result = db.exec(
        "SELECT role FROM repository_memberships WHERE repository_id = $1 AND user_id = $2",
        {std::to_string(repository_id), std::to_string(user_id)});
    if (result.rows.empty()) {
        return std::nullopt;
    }
    return result.rows[0][0];
}

std::vector<DbMembership> list_repository_memberships(PostgresConnection& db, std::int64_t repository_id) {
    const QueryResult result = db.exec(
        "SELECT m.user_id, u.username, m.role FROM repository_memberships m "
        "JOIN users u ON u.id = m.user_id WHERE m.repository_id = $1 ORDER BY u.username",
        {std::to_string(repository_id)});

    std::vector<DbMembership> memberships;
    memberships.reserve(result.rows.size());
    for (const QueryRow& row : result.rows) {
        memberships.push_back(DbMembership{std::stoll(*row[0]), *row[1], *row[2]});
    }
    return memberships;
}

std::int64_t enqueue_job(PostgresConnection& db, std::string_view kind, std::string_view payload, int max_attempts) {
    const QueryResult result = db.exec(
        "INSERT INTO jobs (kind, payload, max_attempts) VALUES ($1, $2, $3) RETURNING id",
        {std::string(kind), std::string(payload), std::to_string(max_attempts)});
    return require_id(result);
}

std::optional<DbJob> claim_next_pending_job(PostgresConnection& db) {
    Transaction transaction(db);
    const QueryResult candidate = db.exec(
        "SELECT id, kind, payload, attempts, max_attempts FROM jobs "
        "WHERE status = 'pending' AND next_attempt_at <= now() "
        "ORDER BY next_attempt_at, id FOR UPDATE SKIP LOCKED LIMIT 1");
    if (candidate.rows.empty()) {
        transaction.commit();
        return std::nullopt;
    }

    const QueryRow& row = candidate.rows[0];
    const std::int64_t id = std::stoll(*row[0]);
    db.exec("UPDATE jobs SET status = 'running', started_at = now() WHERE id = $1", {std::to_string(id)});
    transaction.commit();

    return DbJob{id, *row[1], *row[2], "running", std::stoi(row[3].value_or("0")), std::stoi(row[4].value_or("5"))};
}

void finish_job(PostgresConnection& db, std::int64_t job_id) {
    db.exec(
        "UPDATE jobs SET status = 'done', finished_at = now(), error = NULL WHERE id = $1",
        {std::to_string(job_id)});
}

bool reschedule_job_after_failure(
    PostgresConnection& db, const DbJob& job, std::string_view error_message, std::int64_t backoff_seconds) {
    const int attempts_after_this_one = job.attempts + 1;
    if (attempts_after_this_one < job.max_attempts) {
        db.exec(
            "UPDATE jobs SET status = 'pending', attempts = $2, "
            "next_attempt_at = now() + ($3 || ' seconds')::interval, error = $4 WHERE id = $1",
            {std::to_string(job.id), std::to_string(attempts_after_this_one), std::to_string(backoff_seconds),
             std::string(error_message)});
        return true;
    }
    db.exec(
        "UPDATE jobs SET status = 'failed', attempts = $2, finished_at = now(), error = $3 WHERE id = $1",
        {std::to_string(job.id), std::to_string(attempts_after_this_one), std::string(error_message)});
    return false;
}

} // namespace forge::database

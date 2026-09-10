#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "database/postgres_connection.hpp"

namespace forge::database {

struct DbUser {
    std::int64_t id;
    std::string username;
    std::string password_hash;
};

struct DbRepository {
    std::int64_t id;
    std::string name;
    std::int64_t owner_id;
};

struct DbMembership {
    std::int64_t user_id;
    std::string username;
    std::string role;
};

struct DbJob {
    std::int64_t id;
    std::string kind;
    std::string payload;
    std::string status;
};

// A thin, directly-parameterized data-access layer over the tables
// migrations/0001_initial.sql creates — proving the schema and
// PostgresConnection work end-to-end, not a general ORM: this has
// exactly the query shapes Phase 15/16/17 actually need and no ambition
// to grow more (same "narrow, easy to verify" tradeoff as diff.hpp's
// single-hunk unified diff elsewhere in this codebase).

std::int64_t create_user(PostgresConnection& db, std::string_view username, std::string_view password_hash);
std::optional<DbUser> find_user_by_username(PostgresConnection& db, std::string_view username);
std::optional<DbUser> find_user_by_id(PostgresConnection& db, std::int64_t id);

std::int64_t create_repository(PostgresConnection& db, std::string_view name, std::int64_t owner_id);
std::optional<DbRepository> find_repository_by_name(PostgresConnection& db, std::string_view name);
std::vector<DbRepository> list_repositories(PostgresConnection& db);

// Upsert: replaces the role if `user_id` already has one on `repository_id`.
void set_membership(
    PostgresConnection& db, std::int64_t repository_id, std::int64_t user_id, std::string_view role);
std::optional<std::string> get_membership_role(
    PostgresConnection& db, std::int64_t repository_id, std::int64_t user_id);
std::vector<DbMembership> list_repository_memberships(PostgresConnection& db, std::int64_t repository_id);

std::int64_t enqueue_job(PostgresConnection& db, std::string_view kind, std::string_view payload);

// Atomically claims the oldest pending job (`SELECT ... FOR UPDATE SKIP
// LOCKED`, marking it 'running') so two workers polling concurrently
// can never both claim the same one — the standard Postgres job-queue
// pattern, and this codebase's answer to "Study: ... locking"
// (implementation-plan.md Phase 15). nullopt if the queue is empty.
// Phase 17 (Workers) is what actually calls this in a loop; it's built
// here because the locking behavior belongs with the schema it locks.
std::optional<DbJob> claim_next_pending_job(PostgresConnection& db);
void finish_job(PostgresConnection& db, std::int64_t job_id, bool succeeded, std::string_view error_message);

} // namespace forge::database

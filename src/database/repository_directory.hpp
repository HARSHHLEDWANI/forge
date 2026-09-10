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
    int attempts;
    int max_attempts;
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

// `max_attempts` (default 5) bounds how many times services::WorkerPool
// will retry this job (see reschedule_job_after_failure) before giving
// up on it permanently.
std::int64_t enqueue_job(
    PostgresConnection& db, std::string_view kind, std::string_view payload, int max_attempts = 5);

// Atomically claims the oldest pending job whose backoff delay has
// elapsed (`SELECT ... FOR UPDATE SKIP LOCKED`, marking it 'running')
// so two workers polling concurrently can never both claim the same one
// — the standard Postgres job-queue pattern, and this codebase's answer
// to "Study: ... locking" (implementation-plan.md Phase 15). nullopt if
// the queue has nothing claimable right now.
std::optional<DbJob> claim_next_pending_job(PostgresConnection& db);

// The job succeeded: marks it 'done'.
void finish_job(PostgresConnection& db, std::int64_t job_id);

// The job's handler threw. If `job.attempts + 1 < job.max_attempts`,
// reschedules it — status back to 'pending', next_attempt_at pushed out
// by `backoff_seconds`, `attempts` incremented — for services::WorkerPool
// (or another worker) to retry later, and returns true. Otherwise marks
// it 'failed' permanently (recording `error_message`) and returns
// false. The backoff delay itself is the caller's choice (see
// services::exponential_backoff_seconds) — this function only applies
// whatever delay it's given.
bool reschedule_job_after_failure(
    PostgresConnection& db, const DbJob& job, std::string_view error_message, std::int64_t backoff_seconds);

} // namespace forge::database

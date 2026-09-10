#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "database/postgres_connection.hpp"

namespace forge::database {

struct DbIssue {
    std::int64_t id;
    std::int64_t number;
    std::string title;
    std::string body;
    std::int64_t author_id;
    std::string status;
};

struct DbPullRequest {
    std::int64_t id;
    std::int64_t number;
    std::string title;
    std::string body;
    std::int64_t author_id;
    std::string source_branch;
    std::string target_branch;
    std::string status;
    std::optional<std::string> merge_commit;
};

struct DbReview {
    std::int64_t id;
    std::int64_t reviewer_id;
    std::string state;
    std::string body;
};

struct DbComment {
    std::int64_t id;
    std::int64_t author_id;
    std::string body;
};

struct DbLabel {
    std::int64_t id;
    std::string name;
    std::string color;
};

struct DbBranchProtection {
    bool require_review;
    int required_approvals;
};

// Issues and pull requests share a per-repository sequential `number`
// (like GitHub's #123): assigned inside a transaction that locks the
// repository row first (`SELECT ... FOR UPDATE`, the same mutex-by-row-
// lock idiom repository_directory.hpp's claim_next_pending_job uses),
// so two concurrent creates in the same repository can never collide on
// the same number.
std::int64_t create_issue(
    PostgresConnection& db, std::int64_t repository_id, std::int64_t author_id, std::string_view title,
    std::string_view body, std::int64_t* out_number = nullptr);
std::vector<DbIssue> list_issues(PostgresConnection& db, std::int64_t repository_id);
std::optional<DbIssue> find_issue(PostgresConnection& db, std::int64_t repository_id, std::int64_t number);
void set_issue_status(PostgresConnection& db, std::int64_t issue_id, std::string_view status);

std::int64_t create_pull_request(
    PostgresConnection& db, std::int64_t repository_id, std::int64_t author_id, std::string_view title,
    std::string_view body, std::string_view source_branch, std::string_view target_branch,
    std::int64_t* out_number = nullptr);
std::vector<DbPullRequest> list_pull_requests(PostgresConnection& db, std::int64_t repository_id);
std::optional<DbPullRequest> find_pull_request(PostgresConnection& db, std::int64_t repository_id, std::int64_t number);
void mark_pull_request_merged(PostgresConnection& db, std::int64_t pull_request_id, std::string_view merge_commit_hex);
void set_pull_request_status(PostgresConnection& db, std::int64_t pull_request_id, std::string_view status);

std::int64_t add_review(
    PostgresConnection& db, std::int64_t pull_request_id, std::int64_t reviewer_id, std::string_view state,
    std::string_view body);
std::vector<DbReview> list_reviews(PostgresConnection& db, std::int64_t pull_request_id);
// Counts approvals using only each reviewer's most recent review (a
// reviewer who approved and then requested changes no longer counts) —
// `SELECT DISTINCT ON (reviewer_id) ... ORDER BY reviewer_id, created_at
// DESC`, the standard Postgres "latest row per group" idiom.
int count_current_approvals(PostgresConnection& db, std::int64_t pull_request_id);

std::int64_t create_comment(
    PostgresConnection& db, std::string_view subject_type, std::int64_t subject_id, std::int64_t author_id,
    std::string_view body);
std::vector<DbComment> list_comments(PostgresConnection& db, std::string_view subject_type, std::int64_t subject_id);

std::int64_t create_label(PostgresConnection& db, std::int64_t repository_id, std::string_view name, std::string_view color);
std::vector<DbLabel> list_labels(PostgresConnection& db, std::int64_t repository_id);
void add_label_to_issue(PostgresConnection& db, std::int64_t issue_id, std::int64_t label_id);
std::vector<DbLabel> list_issue_labels(PostgresConnection& db, std::int64_t issue_id);

void set_branch_protection(
    PostgresConnection& db, std::int64_t repository_id, std::string_view branch_name, bool require_review,
    int required_approvals);
std::optional<DbBranchProtection> get_branch_protection(
    PostgresConnection& db, std::int64_t repository_id, std::string_view branch_name);

} // namespace forge::database

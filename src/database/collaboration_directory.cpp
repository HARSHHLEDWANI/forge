#include "database/collaboration_directory.hpp"

#include "core/error.hpp"

namespace forge::database {

namespace {

std::int64_t require_id(const QueryResult& result) {
    if (result.rows.empty() || !result.rows[0].at(0)) {
        throw core::ForgeError("expected an id back from an INSERT ... RETURNING id");
    }
    return std::stoll(*result.rows[0][0]);
}

// Locks the repository row so the "next number" read-then-insert below
// can't race with a concurrent create in the same repository — see
// collaboration_directory.hpp's doc comment.
std::int64_t next_number_for_repository(PostgresConnection& db, std::string_view table, std::int64_t repository_id) {
    db.exec("SELECT id FROM repositories WHERE id = $1 FOR UPDATE", {std::to_string(repository_id)});
    const QueryResult result = db.exec(
        "SELECT COALESCE(MAX(number), 0) + 1 FROM " + std::string(table) + " WHERE repository_id = $1",
        {std::to_string(repository_id)});
    return std::stoll(*result.rows[0][0]);
}

} // namespace

std::int64_t create_issue(
    PostgresConnection& db, std::int64_t repository_id, std::int64_t author_id, std::string_view title,
    std::string_view body, std::int64_t* out_number) {
    Transaction transaction(db);
    const std::int64_t number = next_number_for_repository(db, "issues", repository_id);
    const QueryResult result = db.exec(
        "INSERT INTO issues (repository_id, number, title, body, author_id) VALUES ($1, $2, $3, $4, $5) "
        "RETURNING id",
        {std::to_string(repository_id), std::to_string(number), std::string(title), std::string(body),
         std::to_string(author_id)});
    const std::int64_t id = require_id(result);
    transaction.commit();
    if (out_number != nullptr) {
        *out_number = number;
    }
    return id;
}

std::vector<DbIssue> list_issues(PostgresConnection& db, std::int64_t repository_id) {
    const QueryResult result = db.exec(
        "SELECT id, number, title, body, author_id, status FROM issues WHERE repository_id = $1 ORDER BY number",
        {std::to_string(repository_id)});
    std::vector<DbIssue> issues;
    issues.reserve(result.rows.size());
    for (const QueryRow& row : result.rows) {
        issues.push_back(DbIssue{std::stoll(*row[0]), std::stoll(*row[1]), *row[2], *row[3], std::stoll(*row[4]), *row[5]});
    }
    return issues;
}

std::optional<DbIssue> find_issue(PostgresConnection& db, std::int64_t repository_id, std::int64_t number) {
    const QueryResult result = db.exec(
        "SELECT id, number, title, body, author_id, status FROM issues WHERE repository_id = $1 AND number = $2",
        {std::to_string(repository_id), std::to_string(number)});
    if (result.rows.empty()) {
        return std::nullopt;
    }
    const QueryRow& row = result.rows[0];
    return DbIssue{std::stoll(*row[0]), std::stoll(*row[1]), *row[2], *row[3], std::stoll(*row[4]), *row[5]};
}

void set_issue_status(PostgresConnection& db, std::int64_t issue_id, std::string_view status) {
    db.exec("UPDATE issues SET status = $2 WHERE id = $1", {std::to_string(issue_id), std::string(status)});
}

std::int64_t create_pull_request(
    PostgresConnection& db, std::int64_t repository_id, std::int64_t author_id, std::string_view title,
    std::string_view body, std::string_view source_branch, std::string_view target_branch,
    std::int64_t* out_number) {
    Transaction transaction(db);
    const std::int64_t number = next_number_for_repository(db, "pull_requests", repository_id);
    const QueryResult result = db.exec(
        "INSERT INTO pull_requests (repository_id, number, title, body, author_id, source_branch, target_branch) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id",
        {std::to_string(repository_id), std::to_string(number), std::string(title), std::string(body),
         std::to_string(author_id), std::string(source_branch), std::string(target_branch)});
    const std::int64_t id = require_id(result);
    transaction.commit();
    if (out_number != nullptr) {
        *out_number = number;
    }
    return id;
}

namespace {

DbPullRequest pull_request_from_row(const QueryRow& row) {
    return DbPullRequest{
        std::stoll(*row[0]), std::stoll(*row[1]), *row[2],           *row[3], std::stoll(*row[4]),
        *row[5],              *row[6],             *row[7],           row[8]};
}

} // namespace

std::vector<DbPullRequest> list_pull_requests(PostgresConnection& db, std::int64_t repository_id) {
    const QueryResult result = db.exec(
        "SELECT id, number, title, body, author_id, source_branch, target_branch, status, merge_commit "
        "FROM pull_requests WHERE repository_id = $1 ORDER BY number",
        {std::to_string(repository_id)});
    std::vector<DbPullRequest> pull_requests;
    pull_requests.reserve(result.rows.size());
    for (const QueryRow& row : result.rows) {
        pull_requests.push_back(pull_request_from_row(row));
    }
    return pull_requests;
}

std::optional<DbPullRequest> find_pull_request(PostgresConnection& db, std::int64_t repository_id, std::int64_t number) {
    const QueryResult result = db.exec(
        "SELECT id, number, title, body, author_id, source_branch, target_branch, status, merge_commit "
        "FROM pull_requests WHERE repository_id = $1 AND number = $2",
        {std::to_string(repository_id), std::to_string(number)});
    if (result.rows.empty()) {
        return std::nullopt;
    }
    return pull_request_from_row(result.rows[0]);
}

void mark_pull_request_merged(PostgresConnection& db, std::int64_t pull_request_id, std::string_view merge_commit_hex) {
    db.exec(
        "UPDATE pull_requests SET status = 'merged', merge_commit = $2 WHERE id = $1",
        {std::to_string(pull_request_id), std::string(merge_commit_hex)});
}

void set_pull_request_status(PostgresConnection& db, std::int64_t pull_request_id, std::string_view status) {
    db.exec("UPDATE pull_requests SET status = $2 WHERE id = $1", {std::to_string(pull_request_id), std::string(status)});
}

std::int64_t add_review(
    PostgresConnection& db, std::int64_t pull_request_id, std::int64_t reviewer_id, std::string_view state,
    std::string_view body) {
    const QueryResult result = db.exec(
        "INSERT INTO pull_request_reviews (pull_request_id, reviewer_id, state, body) VALUES ($1, $2, $3, $4) "
        "RETURNING id",
        {std::to_string(pull_request_id), std::to_string(reviewer_id), std::string(state), std::string(body)});
    return require_id(result);
}

std::vector<DbReview> list_reviews(PostgresConnection& db, std::int64_t pull_request_id) {
    const QueryResult result = db.exec(
        "SELECT id, reviewer_id, state, body FROM pull_request_reviews WHERE pull_request_id = $1 ORDER BY id",
        {std::to_string(pull_request_id)});
    std::vector<DbReview> reviews;
    reviews.reserve(result.rows.size());
    for (const QueryRow& row : result.rows) {
        reviews.push_back(DbReview{std::stoll(*row[0]), std::stoll(*row[1]), *row[2], *row[3]});
    }
    return reviews;
}

int count_current_approvals(PostgresConnection& db, std::int64_t pull_request_id) {
    // `id DESC` breaks ties when two reviews from the same reviewer land
    // in the same transaction (and so can share an identical
    // created_at down to its stored precision) — without it,
    // DISTINCT ON's choice of "latest" row for a tie is unspecified,
    // not necessarily the one actually inserted last. `id` is a
    // BIGSERIAL, so it's a reliable insertion-order tiebreaker
    // `created_at` alone isn't.
    const QueryResult result = db.exec(
        "SELECT count(*) FROM ("
        "  SELECT DISTINCT ON (reviewer_id) state FROM pull_request_reviews"
        "  WHERE pull_request_id = $1 ORDER BY reviewer_id, created_at DESC, id DESC"
        ") latest WHERE state = 'approved'",
        {std::to_string(pull_request_id)});
    return std::stoi(result.rows[0][0].value_or("0"));
}

std::int64_t create_comment(
    PostgresConnection& db, std::string_view subject_type, std::int64_t subject_id, std::int64_t author_id,
    std::string_view body) {
    const QueryResult result = db.exec(
        "INSERT INTO comments (subject_type, subject_id, author_id, body) VALUES ($1, $2, $3, $4) RETURNING id",
        {std::string(subject_type), std::to_string(subject_id), std::to_string(author_id), std::string(body)});
    return require_id(result);
}

std::vector<DbComment> list_comments(PostgresConnection& db, std::string_view subject_type, std::int64_t subject_id) {
    const QueryResult result = db.exec(
        "SELECT id, author_id, body FROM comments WHERE subject_type = $1 AND subject_id = $2 ORDER BY id",
        {std::string(subject_type), std::to_string(subject_id)});
    std::vector<DbComment> comments;
    comments.reserve(result.rows.size());
    for (const QueryRow& row : result.rows) {
        comments.push_back(DbComment{std::stoll(*row[0]), std::stoll(*row[1]), *row[2]});
    }
    return comments;
}

std::int64_t create_label(PostgresConnection& db, std::int64_t repository_id, std::string_view name, std::string_view color) {
    const QueryResult result = db.exec(
        "INSERT INTO labels (repository_id, name, color) VALUES ($1, $2, $3) RETURNING id",
        {std::to_string(repository_id), std::string(name), std::string(color)});
    return require_id(result);
}

std::vector<DbLabel> list_labels(PostgresConnection& db, std::int64_t repository_id) {
    const QueryResult result = db.exec(
        "SELECT id, name, color FROM labels WHERE repository_id = $1 ORDER BY name", {std::to_string(repository_id)});
    std::vector<DbLabel> labels;
    labels.reserve(result.rows.size());
    for (const QueryRow& row : result.rows) {
        labels.push_back(DbLabel{std::stoll(*row[0]), *row[1], *row[2]});
    }
    return labels;
}

void add_label_to_issue(PostgresConnection& db, std::int64_t issue_id, std::int64_t label_id) {
    db.exec(
        "INSERT INTO issue_labels (issue_id, label_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
        {std::to_string(issue_id), std::to_string(label_id)});
}

std::vector<DbLabel> list_issue_labels(PostgresConnection& db, std::int64_t issue_id) {
    const QueryResult result = db.exec(
        "SELECT l.id, l.name, l.color FROM issue_labels il JOIN labels l ON l.id = il.label_id "
        "WHERE il.issue_id = $1 ORDER BY l.name",
        {std::to_string(issue_id)});
    std::vector<DbLabel> labels;
    labels.reserve(result.rows.size());
    for (const QueryRow& row : result.rows) {
        labels.push_back(DbLabel{std::stoll(*row[0]), *row[1], *row[2]});
    }
    return labels;
}

void set_branch_protection(
    PostgresConnection& db, std::int64_t repository_id, std::string_view branch_name, bool require_review,
    int required_approvals) {
    db.exec(
        "INSERT INTO branch_protection_rules (repository_id, branch_name, require_review, required_approvals) "
        "VALUES ($1, $2, $3, $4) "
        "ON CONFLICT (repository_id, branch_name) DO UPDATE SET "
        "  require_review = EXCLUDED.require_review, required_approvals = EXCLUDED.required_approvals",
        {std::to_string(repository_id), std::string(branch_name), require_review ? "t" : "f",
         std::to_string(required_approvals)});
}

std::optional<DbBranchProtection> get_branch_protection(
    PostgresConnection& db, std::int64_t repository_id, std::string_view branch_name) {
    const QueryResult result = db.exec(
        "SELECT require_review, required_approvals FROM branch_protection_rules "
        "WHERE repository_id = $1 AND branch_name = $2",
        {std::to_string(repository_id), std::string(branch_name)});
    if (result.rows.empty()) {
        return std::nullopt;
    }
    const QueryRow& row = result.rows[0];
    return DbBranchProtection{row[0].value_or("f") == "t", std::stoi(row[1].value_or("0"))};
}

} // namespace forge::database

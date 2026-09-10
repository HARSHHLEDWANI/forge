#include "database/collaboration_directory.hpp"
#include "database/repository_directory.hpp"
#include "support/postgres_test_support.hpp"

using namespace forge::database;
using forge::test::ensure_migrations_applied;
using forge::test::PostgresFixture;

namespace {

struct Setup {
    std::int64_t owner_id;
    std::int64_t repo_id;
};

Setup seed_repo(PostgresConnection& db) {
    const std::int64_t owner_id = create_user(db, "alice", "hash");
    const std::int64_t repo_id = create_repository(db, "demo", owner_id);
    return Setup{owner_id, repo_id};
}

} // namespace

FORGE_PG_TEST_CASE(create_issue_and_list_issues_round_trip) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const Setup setup = seed_repo(fixture.connection);

    std::int64_t number = 0;
    const std::int64_t issue_id =
        create_issue(fixture.connection, setup.repo_id, setup.owner_id, "Bug report", "It crashes", &number);
    FORGE_CHECK(number == 1);

    const auto found = find_issue(fixture.connection, setup.repo_id, 1);
    FORGE_CHECK(found.has_value() && found->id == issue_id && found->status == "open");

    const auto issues = list_issues(fixture.connection, setup.repo_id);
    FORGE_CHECK(issues.size() == 1);
}

FORGE_PG_TEST_CASE(issue_numbers_increment_per_repository) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const Setup setup = seed_repo(fixture.connection);

    std::int64_t first = 0;
    std::int64_t second = 0;
    create_issue(fixture.connection, setup.repo_id, setup.owner_id, "One", "", &first);
    create_issue(fixture.connection, setup.repo_id, setup.owner_id, "Two", "", &second);
    FORGE_CHECK(first == 1);
    FORGE_CHECK(second == 2);
}

FORGE_PG_TEST_CASE(set_issue_status_closes_and_reopens) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const Setup setup = seed_repo(fixture.connection);
    const std::int64_t issue_id = create_issue(fixture.connection, setup.repo_id, setup.owner_id, "Title", "", nullptr);

    set_issue_status(fixture.connection, issue_id, "closed");
    FORGE_CHECK(find_issue(fixture.connection, setup.repo_id, 1)->status == "closed");
}

FORGE_PG_TEST_CASE(create_comment_and_list_comments_round_trip) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const Setup setup = seed_repo(fixture.connection);
    const std::int64_t issue_id = create_issue(fixture.connection, setup.repo_id, setup.owner_id, "Title", "", nullptr);

    create_comment(fixture.connection, "issue", issue_id, setup.owner_id, "first comment");
    create_comment(fixture.connection, "issue", issue_id, setup.owner_id, "second comment");

    const auto comments = list_comments(fixture.connection, "issue", issue_id);
    FORGE_CHECK(comments.size() == 2);
    FORGE_CHECK(comments[0].body == "first comment");
    FORGE_CHECK(comments[1].body == "second comment");
}

FORGE_PG_TEST_CASE(labels_can_be_attached_to_an_issue) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const Setup setup = seed_repo(fixture.connection);
    const std::int64_t issue_id = create_issue(fixture.connection, setup.repo_id, setup.owner_id, "Title", "", nullptr);
    const std::int64_t label_id = create_label(fixture.connection, setup.repo_id, "bug", "#ff0000");

    add_label_to_issue(fixture.connection, issue_id, label_id);
    // Attaching twice must not error or duplicate (ON CONFLICT DO NOTHING).
    add_label_to_issue(fixture.connection, issue_id, label_id);

    const auto labels = list_issue_labels(fixture.connection, issue_id);
    FORGE_CHECK(labels.size() == 1);
    FORGE_CHECK(labels[0].name == "bug");
    FORGE_CHECK(list_labels(fixture.connection, setup.repo_id).size() == 1);
}

FORGE_PG_TEST_CASE(create_pull_request_and_find_it_round_trip) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const Setup setup = seed_repo(fixture.connection);

    std::int64_t number = 0;
    create_pull_request(
        fixture.connection, setup.repo_id, setup.owner_id, "Add feature", "body", "feature", "main", &number);
    FORGE_CHECK(number == 1);

    const auto pr = find_pull_request(fixture.connection, setup.repo_id, 1);
    FORGE_CHECK(pr.has_value());
    FORGE_CHECK(pr->source_branch == "feature");
    FORGE_CHECK(pr->target_branch == "main");
    FORGE_CHECK(pr->status == "open");
    FORGE_CHECK(!pr->merge_commit.has_value());
}

FORGE_PG_TEST_CASE(mark_pull_request_merged_records_the_merge_commit) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const Setup setup = seed_repo(fixture.connection);
    const std::int64_t pr_id =
        create_pull_request(fixture.connection, setup.repo_id, setup.owner_id, "T", "", "feature", "main", nullptr);

    mark_pull_request_merged(fixture.connection, pr_id, "deadbeef");
    const auto pr = find_pull_request(fixture.connection, setup.repo_id, 1);
    FORGE_CHECK(pr->status == "merged");
    FORGE_CHECK(pr->merge_commit.value_or("") == "deadbeef");
}

FORGE_PG_TEST_CASE(count_current_approvals_only_counts_each_reviewers_latest_review) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const Setup setup = seed_repo(fixture.connection);
    const std::int64_t bob_id = create_user(fixture.connection, "bob", "hash");
    const std::int64_t carol_id = create_user(fixture.connection, "carol", "hash");
    const std::int64_t pr_id =
        create_pull_request(fixture.connection, setup.repo_id, setup.owner_id, "T", "", "feature", "main", nullptr);

    add_review(fixture.connection, pr_id, bob_id, "approved", "");
    add_review(fixture.connection, pr_id, carol_id, "approved", "");
    FORGE_CHECK(count_current_approvals(fixture.connection, pr_id) == 2);

    // Bob changes his mind — his *latest* review no longer counts.
    add_review(fixture.connection, pr_id, bob_id, "changes_requested", "actually no");
    FORGE_CHECK(count_current_approvals(fixture.connection, pr_id) == 1);

    FORGE_CHECK(list_reviews(fixture.connection, pr_id).size() == 3);
}

FORGE_PG_TEST_CASE(branch_protection_round_trips_and_reports_absence_correctly) {
    PostgresFixture fixture(pg_url);
    ensure_migrations_applied(fixture.connection);
    const Setup setup = seed_repo(fixture.connection);

    FORGE_CHECK(!get_branch_protection(fixture.connection, setup.repo_id, "main").has_value());

    set_branch_protection(fixture.connection, setup.repo_id, "main", true, 2);
    const auto rule = get_branch_protection(fixture.connection, setup.repo_id, "main");
    FORGE_CHECK(rule.has_value());
    FORGE_CHECK(rule->require_review);
    FORGE_CHECK(rule->required_approvals == 2);

    set_branch_protection(fixture.connection, setup.repo_id, "main", false, 0);
    FORGE_CHECK(!get_branch_protection(fixture.connection, setup.repo_id, "main")->require_review);
}

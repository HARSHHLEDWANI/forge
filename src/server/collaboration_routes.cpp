#include "server/collaboration_routes.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <sstream>

#include "core/error.hpp"
#include "core/merge.hpp"
#include "database/collaboration_directory.hpp"
#include "database/postgres_connection.hpp"
#include "database/repository_directory.hpp"
#include "domain/permissions.hpp"
#include "server/repo_registry.hpp"
#include "storage/auth_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "transport/http_parser.hpp"

namespace forge::server {

namespace {

using transport::HttpRequest;
using transport::HttpResponse;

std::string query_value(const HttpRequest& request, const std::string& key) {
    const std::map<std::string, std::string> params = transport::parse_query_string(request.query);
    const auto it = params.find(key);
    return it == params.end() ? std::string() : it->second;
}

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::optional<std::string> authenticate(const HttpRequest& request, storage::AuthStore& auth_store) {
    const std::optional<std::string> header = transport::find_header(request.headers, "authorization");
    if (!header || header->rfind("Bearer ", 0) != 0) {
        return std::nullopt;
    }
    return auth_store.authenticate_token(header->substr(std::string_view("Bearer ").size()), now_unix());
}

// Whether `username` may act as at least `required` on `repo_name` —
// including "yes, anyone may" for a repo with no permissions recorded
// at all (see server/app.hpp's "legacy/open" doc comment; the same
// bootstrap-friendly policy POST /object and POST /ref already use).
bool has_role_or_open(
    storage::AuthStore& auth_store, const std::string& repo_name, const std::optional<std::string>& username,
    domain::Role required) {
    if (!auth_store.repo_has_any_permission(repo_name)) {
        return true;
    }
    if (!username) {
        return false;
    }
    const std::optional<domain::Role> role = auth_store.get_permission(repo_name, *username);
    return role && domain::role_satisfies(*role, required);
}

// Postgres's `users` table exists for the collaboration schema's
// foreign keys (repositories.owner_id, issues.author_id, ...); real
// authentication still goes through storage::AuthStore (Phase 14).
// This lazily mirrors an AuthStore username into a Postgres row the
// first time it's needed for one of those references, rather than
// requiring a separate "register in Postgres too" step the user never
// asked for. The password_hash column is unused here — AuthStore's own
// hash is authoritative; a future phase migrating auth fully onto
// Postgres would retire this bridge.
std::int64_t ensure_postgres_user(database::PostgresConnection& db, const std::string& username) {
    if (const std::optional<database::DbUser> existing = database::find_user_by_username(db, username)) {
        return existing->id;
    }
    return database::create_user(db, username, "managed-by-auth-store");
}

std::int64_t ensure_postgres_repository(database::PostgresConnection& db, const std::string& repo_name, std::int64_t owner_id) {
    if (const std::optional<database::DbRepository> existing = database::find_repository_by_name(db, repo_name)) {
        return existing->id;
    }
    return database::create_repository(db, repo_name, owner_id);
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // control character: drop rather than emit invalid JSON
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string issue_json(const database::DbIssue& issue) {
    std::ostringstream out;
    out << "{\"number\":" << issue.number << ",\"title\":\"" << json_escape(issue.title) << "\",\"status\":\""
        << issue.status << "\"}";
    return out.str();
}

std::string pull_request_json(const database::DbPullRequest& pr) {
    std::ostringstream out;
    out << "{\"number\":" << pr.number << ",\"title\":\"" << json_escape(pr.title) << "\",\"source\":\""
        << pr.source_branch << "\",\"target\":\"" << pr.target_branch << "\",\"status\":\"" << pr.status << "\"}";
    return out.str();
}

HttpResponse internal_error(const core::ForgeError& e) {
    return transport::plain_text_response(500, "Internal Server Error", std::string(e.what()) + "\n");
}

HttpResponse not_found(const std::string& what) { return transport::plain_text_response(404, "Not Found", what + "\n"); }
HttpResponse bad_request(const std::string& what) { return transport::plain_text_response(400, "Bad Request", what + "\n"); }
HttpResponse forbidden(const std::string& what) { return transport::plain_text_response(403, "Forbidden", what + "\n"); }
HttpResponse unauthorized() {
    return transport::plain_text_response(401, "Unauthorized", "a valid bearer token is required\n");
}

} // namespace

void wire_collaboration_routes(
    transport::HttpServer& server, const std::filesystem::path& repos_root, const std::filesystem::path& data_root,
    const std::string& database_url) {
    if (database_url.empty()) {
        return;
    }

    server.route("GET", "/repos", [database_url](const HttpRequest&) {
        try {
            database::PostgresConnection db(database_url);
            std::ostringstream out;
            out << "{\"repos\":[";
            bool first = true;
            for (const database::DbRepository& repo : database::list_repositories(db)) {
                if (!first) {
                    out << ',';
                }
                out << "\"" << json_escape(repo.name) << "\"";
                first = false;
            }
            out << "]}";
            return transport::json_response(200, "OK", out.str());
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/issues", [repos_root, data_root, database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const RepoRegistry registry(repos_root);
            if (repo_name.empty() || !registry.exists(repo_name)) {
                return not_found("no such repository");
            }
            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> username = authenticate(request, auth_store);
            if (!username) {
                return unauthorized();
            }
            const std::string title = query_value(request, "title");
            if (title.empty()) {
                return bad_request("title is required");
            }

            database::PostgresConnection db(database_url);
            const std::int64_t author_id = ensure_postgres_user(db, *username);
            const std::int64_t repo_id = ensure_postgres_repository(db, repo_name, author_id);
            std::int64_t number = 0;
            database::create_issue(db, repo_id, author_id, title, request.body, &number);

            return transport::json_response(200, "OK", "{\"number\":" + std::to_string(number) + "}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("GET", "/issues", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            std::ostringstream out;
            out << "{\"issues\":[";
            if (repo) {
                bool first = true;
                for (const database::DbIssue& issue : database::list_issues(db, repo->id)) {
                    if (!first) {
                        out << ',';
                    }
                    out << issue_json(issue);
                    first = false;
                }
            }
            out << "]}";
            return transport::json_response(200, "OK", out.str());
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("GET", "/issue", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const std::optional<std::int64_t> number = [&]() -> std::optional<std::int64_t> {
                const std::string text = query_value(request, "number");
                if (text.empty()) return std::nullopt;
                try { return std::stoll(text); } catch (const std::exception&) { return std::nullopt; }
            }();
            if (!number) {
                return bad_request("a numeric 'number' is required");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found("no such repository");
            }
            const std::optional<database::DbIssue> issue = database::find_issue(db, repo->id, *number);
            if (!issue) {
                return not_found("no such issue");
            }

            std::ostringstream out;
            out << "{\"issue\":" << issue_json(*issue) << ",\"body\":\"" << json_escape(issue->body) << "\",\"comments\":[";
            bool first = true;
            for (const database::DbComment& comment : database::list_comments(db, "issue", issue->id)) {
                if (!first) out << ',';
                out << "\"" << json_escape(comment.body) << "\"";
                first = false;
            }
            out << "],\"labels\":[";
            first = true;
            for (const database::DbLabel& label : database::list_issue_labels(db, issue->id)) {
                if (!first) out << ',';
                out << "\"" << json_escape(label.name) << "\"";
                first = false;
            }
            out << "]}";
            return transport::json_response(200, "OK", out.str());
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/issue/status", [data_root, database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const std::string status = query_value(request, "status");
            if (status != "open" && status != "closed") {
                return bad_request("status must be 'open' or 'closed'");
            }
            storage::AuthStore auth_store(data_root);
            if (!authenticate(request, auth_store)) {
                return unauthorized();
            }
            const std::string number_text = query_value(request, "number");
            std::int64_t number = 0;
            try {
                number = std::stoll(number_text);
            } catch (const std::exception&) {
                return bad_request("a numeric 'number' is required");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found("no such repository");
            }
            const std::optional<database::DbIssue> issue = database::find_issue(db, repo->id, number);
            if (!issue) {
                return not_found("no such issue");
            }
            database::set_issue_status(db, issue->id, status);
            return transport::json_response(200, "OK", "{\"status\":\"" + status + "\"}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/comments", [data_root, database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const std::string subject_type = query_value(request, "subject");
            if (subject_type != "issue" && subject_type != "pull_request") {
                return bad_request("subject must be 'issue' or 'pull_request'");
            }
            if (request.body.empty()) {
                return bad_request("a comment body is required");
            }
            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> username = authenticate(request, auth_store);
            if (!username) {
                return unauthorized();
            }
            const std::string subject_number_text = query_value(request, "number");
            std::int64_t subject_number = 0;
            try {
                subject_number = std::stoll(subject_number_text);
            } catch (const std::exception&) {
                return bad_request("a numeric 'number' is required");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found("no such repository");
            }
            std::optional<std::int64_t> subject_id;
            if (subject_type == "issue") {
                if (const auto issue = database::find_issue(db, repo->id, subject_number)) subject_id = issue->id;
            } else {
                if (const auto pr = database::find_pull_request(db, repo->id, subject_number)) subject_id = pr->id;
            }
            if (!subject_id) {
                return not_found("no such " + subject_type);
            }

            const std::int64_t author_id = ensure_postgres_user(db, *username);
            const std::int64_t comment_id = database::create_comment(db, subject_type, *subject_id, author_id, request.body);
            return transport::json_response(200, "OK", "{\"id\":" + std::to_string(comment_id) + "}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("GET", "/comments", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const std::string subject_type = query_value(request, "subject");
            const std::string subject_number_text = query_value(request, "number");
            std::int64_t subject_number = 0;
            try {
                subject_number = std::stoll(subject_number_text);
            } catch (const std::exception&) {
                return bad_request("a numeric 'number' is required");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found("no such repository");
            }
            std::optional<std::int64_t> subject_id;
            if (subject_type == "issue") {
                if (const auto issue = database::find_issue(db, repo->id, subject_number)) subject_id = issue->id;
            } else if (subject_type == "pull_request") {
                if (const auto pr = database::find_pull_request(db, repo->id, subject_number)) subject_id = pr->id;
            }
            if (!subject_id) {
                return not_found("no such " + subject_type);
            }

            std::ostringstream out;
            out << "{\"comments\":[";
            bool first = true;
            for (const database::DbComment& comment : database::list_comments(db, subject_type, *subject_id)) {
                if (!first) out << ',';
                out << "\"" << json_escape(comment.body) << "\"";
                first = false;
            }
            out << "]}";
            return transport::json_response(200, "OK", out.str());
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/labels", [repos_root, data_root, database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const std::string name = query_value(request, "name");
            std::string color = query_value(request, "color");
            if (color.empty()) {
                color = "#cccccc";
            }
            const RepoRegistry registry(repos_root);
            if (repo_name.empty() || name.empty() || !registry.exists(repo_name)) {
                return bad_request("repo and name are required");
            }
            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> username = authenticate(request, auth_store);
            if (!has_role_or_open(auth_store, repo_name, username, domain::Role::Write)) {
                return username ? forbidden("write access is required") : unauthorized();
            }

            database::PostgresConnection db(database_url);
            const std::int64_t author_id = ensure_postgres_user(db, username.value_or("anonymous"));
            const std::int64_t repo_id = ensure_postgres_repository(db, repo_name, author_id);
            const std::int64_t label_id = database::create_label(db, repo_id, name, color);
            return transport::json_response(200, "OK", "{\"id\":" + std::to_string(label_id) + "}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("GET", "/labels", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            std::ostringstream out;
            out << "{\"labels\":[";
            if (repo) {
                bool first = true;
                for (const database::DbLabel& label : database::list_labels(db, repo->id)) {
                    if (!first) out << ',';
                    out << "{\"name\":\"" << json_escape(label.name) << "\",\"color\":\"" << label.color << "\"}";
                    first = false;
                }
            }
            out << "]}";
            return transport::json_response(200, "OK", out.str());
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/issue/labels", [repos_root, data_root, database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const std::string label_name = query_value(request, "label");
            const RepoRegistry registry(repos_root);
            if (repo_name.empty() || label_name.empty() || !registry.exists(repo_name)) {
                return bad_request("repo and label are required");
            }
            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> username = authenticate(request, auth_store);
            if (!has_role_or_open(auth_store, repo_name, username, domain::Role::Write)) {
                return username ? forbidden("write access is required") : unauthorized();
            }
            std::int64_t number = 0;
            try {
                number = std::stoll(query_value(request, "number"));
            } catch (const std::exception&) {
                return bad_request("a numeric 'number' is required");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found("no such repository");
            }
            const std::optional<database::DbIssue> issue = database::find_issue(db, repo->id, number);
            if (!issue) {
                return not_found("no such issue");
            }
            const std::vector<database::DbLabel> labels = database::list_labels(db, repo->id);
            const auto label_it = std::find_if(
                labels.begin(), labels.end(), [&](const database::DbLabel& l) { return l.name == label_name; });
            if (label_it == labels.end()) {
                return not_found("no such label");
            }
            database::add_label_to_issue(db, issue->id, label_it->id);
            return transport::json_response(200, "OK", "{\"attached\":true}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/pulls", [repos_root, data_root, database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const std::string title = query_value(request, "title");
            const std::string source_branch = query_value(request, "source");
            const std::string target_branch = query_value(request, "target");
            const RepoRegistry registry(repos_root);
            if (repo_name.empty() || title.empty() || source_branch.empty() || target_branch.empty() ||
                !registry.exists(repo_name)) {
                return bad_request("repo, title, source, and target are required");
            }
            storage::RefStore repo_refs(registry.forge_dir_for(repo_name));
            if (!repo_refs.branch_exists(source_branch) || !repo_refs.branch_exists(target_branch)) {
                return bad_request("source and target must both be existing branches");
            }

            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> username = authenticate(request, auth_store);
            if (!username) {
                return unauthorized();
            }

            database::PostgresConnection db(database_url);
            const std::int64_t author_id = ensure_postgres_user(db, *username);
            const std::int64_t repo_id = ensure_postgres_repository(db, repo_name, author_id);
            std::int64_t number = 0;
            database::create_pull_request(db, repo_id, author_id, title, request.body, source_branch, target_branch, &number);
            return transport::json_response(200, "OK", "{\"number\":" + std::to_string(number) + "}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("GET", "/pulls", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            std::ostringstream out;
            out << "{\"pulls\":[";
            if (repo) {
                bool first = true;
                for (const database::DbPullRequest& pr : database::list_pull_requests(db, repo->id)) {
                    if (!first) out << ',';
                    out << pull_request_json(pr);
                    first = false;
                }
            }
            out << "]}";
            return transport::json_response(200, "OK", out.str());
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("GET", "/pull", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            std::int64_t number = 0;
            try {
                number = std::stoll(query_value(request, "number"));
            } catch (const std::exception&) {
                return bad_request("a numeric 'number' is required");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found("no such repository");
            }
            const std::optional<database::DbPullRequest> pr = database::find_pull_request(db, repo->id, number);
            if (!pr) {
                return not_found("no such pull request");
            }

            std::ostringstream out;
            out << "{\"pull\":" << pull_request_json(*pr) << ",\"body\":\"" << json_escape(pr->body)
                << "\",\"approvals\":" << database::count_current_approvals(db, pr->id) << ",\"reviews\":[";
            bool first = true;
            for (const database::DbReview& review : database::list_reviews(db, pr->id)) {
                if (!first) out << ',';
                out << "{\"state\":\"" << review.state << "\",\"body\":\"" << json_escape(review.body) << "\"}";
                first = false;
            }
            out << "]}";
            return transport::json_response(200, "OK", out.str());
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/pulls/review", [data_root, database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const std::string state = query_value(request, "state");
            if (state != "approved" && state != "changes_requested" && state != "commented") {
                return bad_request("state must be 'approved', 'changes_requested', or 'commented'");
            }
            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> username = authenticate(request, auth_store);
            if (!username) {
                return unauthorized();
            }
            std::int64_t number = 0;
            try {
                number = std::stoll(query_value(request, "number"));
            } catch (const std::exception&) {
                return bad_request("a numeric 'number' is required");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found("no such repository");
            }
            const std::optional<database::DbPullRequest> pr = database::find_pull_request(db, repo->id, number);
            if (!pr) {
                return not_found("no such pull request");
            }
            const std::int64_t reviewer_id = ensure_postgres_user(db, *username);
            const std::int64_t review_id = database::add_review(db, pr->id, reviewer_id, state, request.body);
            return transport::json_response(200, "OK", "{\"id\":" + std::to_string(review_id) + "}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/pulls/merge", [repos_root, data_root, database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const RepoRegistry registry(repos_root);
            if (repo_name.empty() || !registry.exists(repo_name)) {
                return not_found("no such repository");
            }
            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> username = authenticate(request, auth_store);
            if (!has_role_or_open(auth_store, repo_name, username, domain::Role::Write)) {
                return username ? forbidden("write access is required") : unauthorized();
            }
            std::int64_t number = 0;
            try {
                number = std::stoll(query_value(request, "number"));
            } catch (const std::exception&) {
                return bad_request("a numeric 'number' is required");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found("no such repository");
            }
            const std::optional<database::DbPullRequest> pr = database::find_pull_request(db, repo->id, number);
            if (!pr) {
                return not_found("no such pull request");
            }
            if (pr->status != "open") {
                return bad_request("pull request is not open");
            }

            if (const std::optional<database::DbBranchProtection> rule =
                    database::get_branch_protection(db, repo->id, pr->target_branch)) {
                if (rule->require_review && database::count_current_approvals(db, pr->id) < rule->required_approvals) {
                    return forbidden(
                        "branch '" + pr->target_branch + "' requires " + std::to_string(rule->required_approvals) +
                        " approval(s); has " + std::to_string(database::count_current_approvals(db, pr->id)));
                }
            }

            const std::filesystem::path forge_dir = registry.forge_dir_for(repo_name);
            const storage::RepositoryConfig config = storage::load_config(forge_dir);
            storage::ObjectStore objects(config.storage_root);
            storage::RefStore repo_refs(forge_dir);
            const std::optional<core::ObjectId> source_commit = repo_refs.read_branch(pr->source_branch);
            if (!source_commit) {
                return bad_request("source branch no longer exists");
            }

            const core::BareMergeResult merge_result = core::merge_in_object_store(
                objects, repo_refs, *source_commit, pr->target_branch, *username,
                "Merge pull request #" + std::to_string(pr->number) + ": " + pr->title, now_unix());

            if (merge_result.outcome == core::MergeOutcome::Conflict) {
                std::ostringstream out;
                out << "{\"conflict\":true,\"paths\":[";
                bool first = true;
                for (const core::MergeConflict& conflict : merge_result.conflicts) {
                    if (!first) out << ',';
                    out << "\"" << json_escape(conflict.path) << "\"";
                    first = false;
                }
                out << "]}";
                return transport::json_response(409, "Conflict", out.str());
            }

            const std::string merge_commit_hex =
                merge_result.commit_id ? merge_result.commit_id->to_hex() : std::string();
            database::mark_pull_request_merged(db, pr->id, merge_commit_hex);
            return transport::json_response(200, "OK", "{\"merged\":true,\"commit\":\"" + merge_commit_hex + "\"}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/branch-protection", [repos_root, data_root, database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            const std::string branch_name = query_value(request, "branch");
            const RepoRegistry registry(repos_root);
            if (repo_name.empty() || branch_name.empty() || !registry.exists(repo_name)) {
                return bad_request("repo and branch are required");
            }
            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> username = authenticate(request, auth_store);
            if (!has_role_or_open(auth_store, repo_name, username, domain::Role::Admin)) {
                return username ? forbidden("admin access is required") : unauthorized();
            }
            const bool require_review = query_value(request, "require_review") != "false";
            int required_approvals = 1;
            try {
                const std::string text = query_value(request, "required_approvals");
                if (!text.empty()) {
                    required_approvals = std::stoi(text);
                }
            } catch (const std::exception&) {
                return bad_request("required_approvals must be a number");
            }

            database::PostgresConnection db(database_url);
            const std::int64_t author_id = ensure_postgres_user(db, username.value_or("anonymous"));
            const std::int64_t repo_id = ensure_postgres_repository(db, repo_name, author_id);
            database::set_branch_protection(db, repo_id, branch_name, require_review, required_approvals);
            return transport::json_response(200, "OK", "{\"protected\":true}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });
}

} // namespace forge::server

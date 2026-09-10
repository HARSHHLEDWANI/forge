#include "server/web_ui.hpp"

#include <sstream>

#include "core/error.hpp"
#include "database/collaboration_directory.hpp"
#include "database/postgres_connection.hpp"
#include "database/repository_directory.hpp"
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

std::string html_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

HttpResponse html_page(const std::string& title, const std::string& body_html) {
    std::ostringstream out;
    out << "<!doctype html><html><head><meta charset=\"utf-8\"><title>" << html_escape(title)
        << "</title></head><body>" << body_html << "</body></html>";
    HttpResponse response;
    response.status = 200;
    response.status_text = "OK";
    response.headers["content-type"] = "text/html; charset=utf-8";
    response.body = out.str();
    return response;
}

HttpResponse not_found_page(const std::string& what) {
    HttpResponse response = html_page("Not found", "<p>" + html_escape(what) + "</p>");
    response.status = 404;
    response.status_text = "Not Found";
    return response;
}

} // namespace

void wire_web_ui(transport::HttpServer& server, const std::filesystem::path& repos_root, const std::string& database_url) {
    if (database_url.empty()) {
        return;
    }
    (void)repos_root; // reserved for a future page that also shows branches/refs alongside issues/PRs

    server.route("GET", "/ui", [database_url](const HttpRequest&) {
        try {
            database::PostgresConnection db(database_url);
            std::ostringstream body;
            body << "<h1>Repositories</h1><ul>";
            for (const database::DbRepository& repo : database::list_repositories(db)) {
                const std::string name = html_escape(repo.name);
                body << "<li><a href=\"/ui/repo?repo=" << name << "\">" << name << "</a></li>";
            }
            body << "</ul>";
            return html_page("Forge", body.str());
        } catch (const core::ForgeError& e) {
            return html_page("Error", "<p>" + html_escape(e.what()) + "</p>");
        }
    });

    server.route("GET", "/ui/repo", [database_url](const HttpRequest& request) {
        const std::string repo_name = query_value(request, "repo");
        std::ostringstream body;
        body << "<h1>" << html_escape(repo_name) << "</h1><ul>"
             << "<li><a href=\"/ui/issues?repo=" << html_escape(repo_name) << "\">Issues</a></li>"
             << "<li><a href=\"/ui/pulls?repo=" << html_escape(repo_name) << "\">Pull requests</a></li>"
             << "</ul><p><a href=\"/ui\">&larr; all repositories</a></p>";
        return html_page(repo_name, body.str());
    });

    server.route("GET", "/ui/issues", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found_page("no such repository: " + repo_name);
            }

            std::ostringstream body;
            body << "<h1>Issues &middot; " << html_escape(repo_name) << "</h1><ul>";
            for (const database::DbIssue& issue : database::list_issues(db, repo->id)) {
                body << "<li>#" << issue.number << " <a href=\"/ui/issue?repo=" << html_escape(repo_name)
                     << "&number=" << issue.number << "\">" << html_escape(issue.title) << "</a> ["
                     << html_escape(issue.status) << "]</li>";
            }
            body << "</ul><p><a href=\"/ui/repo?repo=" << html_escape(repo_name) << "\">&larr; " << html_escape(repo_name)
                 << "</a></p>";
            return html_page("Issues - " + repo_name, body.str());
        } catch (const core::ForgeError& e) {
            return html_page("Error", "<p>" + html_escape(e.what()) + "</p>");
        }
    });

    server.route("GET", "/ui/issue", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            std::int64_t number = 0;
            try {
                number = std::stoll(query_value(request, "number"));
            } catch (const std::exception&) {
                return not_found_page("invalid issue number");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found_page("no such repository: " + repo_name);
            }
            const std::optional<database::DbIssue> issue = database::find_issue(db, repo->id, number);
            if (!issue) {
                return not_found_page("no such issue: #" + std::to_string(number));
            }

            std::ostringstream body;
            body << "<h1>#" << issue->number << " " << html_escape(issue->title) << " [" << html_escape(issue->status)
                 << "]</h1><p>" << html_escape(issue->body) << "</p><h2>Labels</h2><ul>";
            for (const database::DbLabel& label : database::list_issue_labels(db, issue->id)) {
                body << "<li>" << html_escape(label.name) << "</li>";
            }
            body << "</ul><h2>Comments</h2><ul>";
            for (const database::DbComment& comment : database::list_comments(db, "issue", issue->id)) {
                body << "<li>" << html_escape(comment.body) << "</li>";
            }
            body << "</ul><p><a href=\"/ui/issues?repo=" << html_escape(repo_name) << "\">&larr; issues</a></p>";
            return html_page("#" + std::to_string(issue->number) + " " + issue->title, body.str());
        } catch (const core::ForgeError& e) {
            return html_page("Error", "<p>" + html_escape(e.what()) + "</p>");
        }
    });

    server.route("GET", "/ui/pulls", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found_page("no such repository: " + repo_name);
            }

            std::ostringstream body;
            body << "<h1>Pull requests &middot; " << html_escape(repo_name) << "</h1><ul>";
            for (const database::DbPullRequest& pr : database::list_pull_requests(db, repo->id)) {
                body << "<li>#" << pr.number << " <a href=\"/ui/pull?repo=" << html_escape(repo_name)
                     << "&number=" << pr.number << "\">" << html_escape(pr.title) << "</a> (" << html_escape(pr.source_branch)
                     << " &rarr; " << html_escape(pr.target_branch) << ") [" << html_escape(pr.status) << "]</li>";
            }
            body << "</ul><p><a href=\"/ui/repo?repo=" << html_escape(repo_name) << "\">&larr; " << html_escape(repo_name)
                 << "</a></p>";
            return html_page("Pull requests - " + repo_name, body.str());
        } catch (const core::ForgeError& e) {
            return html_page("Error", "<p>" + html_escape(e.what()) + "</p>");
        }
    });

    server.route("GET", "/ui/pull", [database_url](const HttpRequest& request) {
        try {
            const std::string repo_name = query_value(request, "repo");
            std::int64_t number = 0;
            try {
                number = std::stoll(query_value(request, "number"));
            } catch (const std::exception&) {
                return not_found_page("invalid pull request number");
            }

            database::PostgresConnection db(database_url);
            const std::optional<database::DbRepository> repo = database::find_repository_by_name(db, repo_name);
            if (!repo) {
                return not_found_page("no such repository: " + repo_name);
            }
            const std::optional<database::DbPullRequest> pr = database::find_pull_request(db, repo->id, number);
            if (!pr) {
                return not_found_page("no such pull request: #" + std::to_string(number));
            }

            std::ostringstream body;
            body << "<h1>#" << pr->number << " " << html_escape(pr->title) << " [" << html_escape(pr->status)
                 << "]</h1><p>" << html_escape(pr->source_branch) << " &rarr; " << html_escape(pr->target_branch)
                 << "</p><p>" << html_escape(pr->body) << "</p>"
                 << "<p>Approvals: " << database::count_current_approvals(db, pr->id) << "</p><h2>Reviews</h2><ul>";
            for (const database::DbReview& review : database::list_reviews(db, pr->id)) {
                body << "<li>[" << html_escape(review.state) << "] " << html_escape(review.body) << "</li>";
            }
            body << "</ul><h2>Comments</h2><ul>";
            for (const database::DbComment& comment : database::list_comments(db, "pull_request", pr->id)) {
                body << "<li>" << html_escape(comment.body) << "</li>";
            }
            body << "</ul><p><a href=\"/ui/pulls?repo=" << html_escape(repo_name) << "\">&larr; pull requests</a></p>";
            return html_page("#" + std::to_string(pr->number) + " " + pr->title, body.str());
        } catch (const core::ForgeError& e) {
            return html_page("Error", "<p>" + html_escape(e.what()) + "</p>");
        }
    });
}

} // namespace forge::server

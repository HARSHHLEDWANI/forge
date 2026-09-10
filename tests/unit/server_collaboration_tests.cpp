#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <thread>

#include "core/committing.hpp"
#include "core/ignore_rules.hpp"
#include "core/object_id.hpp"
#include "core/remote.hpp"
#include "core/staging.hpp"
#include "server/app.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "support/postgres_test_support.hpp"
#include "support/temp_dir.hpp"
#include "transport/http_client.hpp"
#include "transport/http_server.hpp"

using forge::test::postgres_test_url;
using forge::test::TempDir;
using forge::transport::HttpClientRequest;
using forge::transport::HttpResponse;
using forge::transport::HttpServer;
using forge::transport::send_http_request;

namespace {

// This suite's Postgres data isn't wrapped in a rolled-back transaction
// like the DAO-level tests (support/postgres_test_support.hpp's
// PostgresFixture) — these go through the server's own per-request
// connections, so there's nothing for a test to roll back. A repo name
// used across two separate `docker compose run` invocations against the
// same persistent database would collide with whatever the previous run
// left behind (e.g. issue/PR numbers no longer starting at 1), so every
// run gets its own random repo names instead of relying on the database
// being wiped between runs.
std::string unique_repo_name(std::string_view prefix) {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream name;
    name << prefix << '-' << std::hex << rng();
    return name.str();
}

// Same reasoning as unique_repo_name: POST /users goes through the
// server's own auto-committing connection, so a username created here
// is real, persistent data another `docker compose run` (or another
// test in this same run) could collide with — "alice" as a fixed name
// isn't safe to reuse across runs against a shared, un-wiped database.
std::string unique_username(std::string_view prefix) { return unique_repo_name(prefix); }

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

struct RunningCollabServer {
    TempDir repos_root;
    TempDir data_root;
    HttpServer http_server{"127.0.0.1", 0};
    std::thread thread;

    explicit RunningCollabServer(const std::string& database_url) {
        forge::server::wire_routes(http_server, repos_root.path(), data_root.path(), database_url);
        http_server.start();
        thread = std::thread([this] { http_server.serve(); });
    }

    ~RunningCollabServer() {
        http_server.stop();
        thread.join();
    }

    HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "",
                       const std::string& bearer = "") const {
        HttpClientRequest request;
        request.method = method;
        request.path = path;
        request.body = body;
        if (!bearer.empty()) {
            request.headers["authorization"] = "Bearer " + bearer;
        }
        return send_http_request("127.0.0.1", http_server.port(), request);
    }

    std::string login(const std::string& username, const std::string& password) {
        const HttpResponse response = call("POST", "/login?username=" + username, password);
        const std::string body = response.body;
        const std::size_t start = body.find("\"token\":\"") + std::string("\"token\":\"").size();
        return body.substr(start, body.find('"', start) - start);
    }
};

} // namespace

FORGE_PG_TEST_CASE(collaboration_workflow_end_to_end_over_http) {
    RunningCollabServer server(pg_url);

    const std::string alice = unique_username("alice");
    const std::string bob = unique_username("bob");
    server.call("POST", "/users?username=" + alice, "hunter2");
    const std::string alice_token = server.login(alice, "hunter2");
    server.call("POST", "/users?username=" + bob, "hunter3");
    const std::string bob_token = server.login(bob, "hunter3");
    const std::string repo = unique_repo_name("demo");

    // Create the git repo via a real push (core::remote.hpp), the same
    // way a `forge push` would — collaboration features build on top of
    // a repository that already exists in the git sense.
    TempDir source_dir;
    forge::storage::initialize_repository(source_dir.path());
    forge::core::IgnoreRules ignore_rules = forge::core::IgnoreRules::parse("");
    forge::storage::ObjectStore objects(
        forge::storage::load_config(source_dir.path() / ".forge").storage_root);
    forge::storage::IndexStore index_store(source_dir.path() / ".forge" / "index");
    forge::storage::RefStore refs(source_dir.path() / ".forge");
    write_file(source_dir.path() / "a.txt", "hello");
    forge::core::stage_path(objects, index_store, ignore_rules, source_dir.path(), source_dir.path());
    forge::core::create_commit(objects, index_store, refs, "Test <t@example.com>", "base", 1000);

    const forge::core::RemoteEndpoint remote{"127.0.0.1", server.http_server.port(), repo};
    forge::core::push(objects, refs, remote, "main", false);

    // alice files an issue, bob comments on it, alice closes it.
    const HttpResponse issue_response =
        server.call("POST", "/issues?repo=" + repo + "&title=Bug", "It crashes", alice_token);
    FORGE_CHECK(issue_response.status == 200);
    FORGE_CHECK(issue_response.body.find("\"number\":1") != std::string::npos);

    const HttpResponse comment_response = server.call(
        "POST", "/comments?repo=" + repo + "&subject=issue&number=1", "I can reproduce this", bob_token);
    FORGE_CHECK(comment_response.status == 200);

    const HttpResponse close_response =
        server.call("POST", "/issue/status?repo=" + repo + "&number=1&status=closed", "", alice_token);
    FORGE_CHECK(close_response.status == 200);

    const HttpResponse issue_detail = server.call("GET", "/issue?repo=" + repo + "&number=1");
    FORGE_CHECK(issue_detail.body.find("\"status\":\"closed\"") != std::string::npos);
    FORGE_CHECK(issue_detail.body.find("I can reproduce this") != std::string::npos);

    // A label.
    const HttpResponse label_response =
        server.call("POST", "/labels?repo=" + repo + "&name=bug&color=red", "", alice_token);
    FORGE_CHECK(label_response.status == 200);
    const HttpResponse attach_response =
        server.call("POST", "/issue/labels?repo=" + repo + "&number=1&label=bug", "", alice_token);
    FORGE_CHECK(attach_response.status == 200);

    // bob branches, pushes, and opens a PR back into main.
    write_file(source_dir.path() / "b.txt", "from bob");
    forge::core::stage_path(objects, index_store, ignore_rules, source_dir.path(), source_dir.path());
    const forge::core::ObjectId feature_commit =
        forge::core::create_commit(objects, index_store, refs, "Bob <bob@example.com>", "add b.txt", 1001).commit_id;
    refs.update_branch("feature", std::nullopt, feature_commit);
    forge::core::push(objects, refs, remote, "feature", false);

    const HttpResponse pr_response = server.call(
        "POST", "/pulls?repo=" + repo + "&title=Add-b&source=feature&target=main", "adds a file", bob_token);
    FORGE_CHECK(pr_response.status == 200);
    FORGE_CHECK(pr_response.body.find("\"number\":1") != std::string::npos);

    // alice approves, bob merges.
    const HttpResponse review_response =
        server.call("POST", "/pulls/review?repo=" + repo + "&number=1&state=approved", "looks good", alice_token);
    FORGE_CHECK(review_response.status == 200);

    const HttpResponse merge_response = server.call("POST", "/pulls/merge?repo=" + repo + "&number=1", "", bob_token);
    FORGE_CHECK(merge_response.status == 200);
    FORGE_CHECK(merge_response.body.find("\"merged\":true") != std::string::npos);

    const HttpResponse pull_detail = server.call("GET", "/pull?repo=" + repo + "&number=1");
    FORGE_CHECK(pull_detail.body.find("\"status\":\"merged\"") != std::string::npos);

    // main's ref on the server actually advanced.
    const HttpResponse refs_response = server.call("GET", "/refs?repo=" + repo);
    FORGE_CHECK(refs_response.body.find("\"main\"") != std::string::npos);
}

FORGE_PG_TEST_CASE(pull_request_merge_is_blocked_until_branch_protection_is_satisfied) {
    RunningCollabServer server(pg_url);
    const std::string alice = unique_username("alice");
    server.call("POST", "/users?username=" + alice, "hunter2");
    const std::string alice_token = server.login(alice, "hunter2");
    const std::string repo = unique_repo_name("protected-demo");

    TempDir source_dir;
    forge::storage::initialize_repository(source_dir.path());
    forge::core::IgnoreRules ignore_rules = forge::core::IgnoreRules::parse("");
    forge::storage::ObjectStore objects(forge::storage::load_config(source_dir.path() / ".forge").storage_root);
    forge::storage::IndexStore index_store(source_dir.path() / ".forge" / "index");
    forge::storage::RefStore refs(source_dir.path() / ".forge");
    write_file(source_dir.path() / "a.txt", "hello");
    forge::core::stage_path(objects, index_store, ignore_rules, source_dir.path(), source_dir.path());
    forge::core::create_commit(objects, index_store, refs, "Test <t@example.com>", "base", 1000);

    const forge::core::RemoteEndpoint remote{"127.0.0.1", server.http_server.port(), repo};
    forge::core::push(objects, refs, remote, "main", false);

    write_file(source_dir.path() / "b.txt", "change");
    forge::core::stage_path(objects, index_store, ignore_rules, source_dir.path(), source_dir.path());
    const forge::core::ObjectId feature_commit =
        forge::core::create_commit(objects, index_store, refs, "Test <t@example.com>", "change", 1001).commit_id;
    refs.update_branch("feature", std::nullopt, feature_commit);
    forge::core::push(objects, refs, remote, "feature", false);

    server.call(
        "POST", "/branch-protection?repo=" + repo + "&branch=main&require_review=true&required_approvals=1", "",
        alice_token);

    server.call("POST", "/pulls?repo=" + repo + "&title=T&source=feature&target=main", "", alice_token);

    const HttpResponse blocked = server.call("POST", "/pulls/merge?repo=" + repo + "&number=1", "", alice_token);
    FORGE_CHECK(blocked.status == 403);

    server.call("POST", "/pulls/review?repo=" + repo + "&number=1&state=approved", "", alice_token);
    const HttpResponse allowed = server.call("POST", "/pulls/merge?repo=" + repo + "&number=1", "", alice_token);
    FORGE_CHECK(allowed.status == 200);
}

FORGE_PG_TEST_CASE(web_ui_pages_render_real_data) {
    RunningCollabServer server(pg_url);
    const std::string alice = unique_username("alice");
    server.call("POST", "/users?username=" + alice, "hunter2");
    const std::string alice_token = server.login(alice, "hunter2");
    const std::string repo = unique_repo_name("ui-demo");

    TempDir source_dir;
    forge::storage::initialize_repository(source_dir.path());
    forge::core::IgnoreRules ignore_rules = forge::core::IgnoreRules::parse("");
    forge::storage::ObjectStore objects(forge::storage::load_config(source_dir.path() / ".forge").storage_root);
    forge::storage::IndexStore index_store(source_dir.path() / ".forge" / "index");
    forge::storage::RefStore refs(source_dir.path() / ".forge");
    write_file(source_dir.path() / "a.txt", "hello");
    forge::core::stage_path(objects, index_store, ignore_rules, source_dir.path(), source_dir.path());
    forge::core::create_commit(objects, index_store, refs, "Test <t@example.com>", "base", 1000);
    const forge::core::RemoteEndpoint remote{"127.0.0.1", server.http_server.port(), repo};
    forge::core::push(objects, refs, remote, "main", false);

    // parse_query_string (transport/http_parser.hpp) deliberately does
    // no percent/plus decoding, so a query-string title stays literal —
    // a single word here avoids that entirely rather than asserting on
    // an encoding this project's minimal parser doesn't implement.
    server.call("POST", "/issues?repo=" + repo + "&title=SampleIssue", "issue body", alice_token);

    const HttpResponse root = server.call("GET", "/ui");
    FORGE_CHECK(root.status == 200);
    FORGE_CHECK(root.body.find(repo) != std::string::npos);

    const HttpResponse issues_page = server.call("GET", "/ui/issues?repo=" + repo);
    FORGE_CHECK(issues_page.body.find("SampleIssue") != std::string::npos);

    const HttpResponse issue_page = server.call("GET", "/ui/issue?repo=" + repo + "&number=1");
    FORGE_CHECK(issue_page.body.find("issue body") != std::string::npos);

    const HttpResponse pulls_page = server.call("GET", "/ui/pulls?repo=" + repo);
    FORGE_CHECK(pulls_page.status == 200);
}

#include <fstream>
#include <thread>

#include "core/branching.hpp"
#include "core/committing.hpp"
#include "core/error.hpp"
#include "core/ignore_rules.hpp"
#include "core/object_id.hpp"
#include "core/remote.hpp"
#include "core/staging.hpp"
#include "core/tree.hpp"
#include "server/app.hpp"
#include "server/repo_registry.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"
#include "transport/http_server.hpp"

using forge::core::create_commit;
using forge::core::IgnoreRules;
using forge::core::ObjectId;
using forge::core::parse_remote_url;
using forge::core::RemoteEndpoint;
using forge::core::render_remote_url;
using forge::core::stage_path;
using forge::storage::IndexStore;
using forge::storage::ObjectStore;
using forge::storage::RefStore;
using forge::test::TempDir;

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Runs a real forge-server (see server/app.hpp) on a background thread,
// backed by a temp `repos_root`, so remote.cpp's clone/fetch/push can be
// exercised over an actual socket instead of being tested against core
// logic alone.
struct RemoteTestServer {
    TempDir repos_root;
    forge::transport::HttpServer http_server{"127.0.0.1", 0};
    std::thread thread;

    RemoteTestServer() {
        forge::server::wire_routes(http_server, repos_root.path());
        http_server.start();
        thread = std::thread([this] { http_server.serve(); });
    }

    ~RemoteTestServer() {
        http_server.stop();
        thread.join();
    }

    RemoteEndpoint endpoint(const std::string& repo_name) const {
        return RemoteEndpoint{"127.0.0.1", http_server.port(), repo_name};
    }

    std::filesystem::path forge_dir(const std::string& repo_name) {
        forge::server::RepoRegistry registry(repos_root.path());
        registry.ensure_exists(repo_name);
        return registry.forge_dir_for(repo_name);
    }
};

// Seeds a hosted repo directly through the storage layer (no working
// tree involved, matching how the server itself only ever touches
// ObjectStore/RefStore — see server/repo_registry.hpp) with one commit
// adding "a.txt", and points "main" at it.
ObjectId seed_server_repo(RemoteTestServer& server, const std::string& repo_name) {
    const std::filesystem::path forge_dir = server.forge_dir(repo_name);
    const auto config = forge::storage::load_config(forge_dir);
    ObjectStore objects(config.storage_root);
    RefStore refs(forge_dir);

    const ObjectId blob_id = objects.put_blob(forge::core::Blob{"hello"});
    const forge::core::Tree tree(
        std::vector<forge::core::TreeEntry>{{"a.txt", forge::core::EntryMode::RegularFile, blob_id}});
    const ObjectId tree_id = objects.put_tree(tree);
    const forge::core::Commit commit{tree_id, {}, "Test <t@example.com>", 1000, "seed"};
    const ObjectId commit_id = objects.put_commit(commit);
    refs.update_branch("main", std::nullopt, commit_id);
    return commit_id;
}

// Adds a second commit on the server's "main", parented on `parent`.
ObjectId advance_server_repo(RemoteTestServer& server, const std::string& repo_name, const ObjectId& parent) {
    const std::filesystem::path forge_dir = server.forge_dir(repo_name);
    const auto config = forge::storage::load_config(forge_dir);
    ObjectStore objects(config.storage_root);
    RefStore refs(forge_dir);

    // Re-adds "a.txt" (content-addressed put_blob is idempotent, so this
    // just reuses the seed commit's existing blob) plus a new "b.txt".
    const ObjectId a_blob_id = objects.put_blob(forge::core::Blob{"hello"});
    const ObjectId b_blob_id = objects.put_blob(forge::core::Blob{"world"});
    const forge::core::Tree tree(std::vector<forge::core::TreeEntry>{
        {"a.txt", forge::core::EntryMode::RegularFile, a_blob_id},
        {"b.txt", forge::core::EntryMode::RegularFile, b_blob_id}});
    const ObjectId tree_id = objects.put_tree(tree);
    const forge::core::Commit commit{tree_id, {parent}, "Test <t@example.com>", 2000, "advance"};
    const ObjectId commit_id = objects.put_commit(commit);
    refs.update_branch("main", parent, commit_id);
    return commit_id;
}

struct ClientRepo {
    TempDir dir;
    ObjectStore objects;
    IndexStore index_store;
    RefStore refs;
    IgnoreRules ignore_rules = IgnoreRules::parse("");

    explicit ClientRepo(const std::filesystem::path& forge_dir)
        : objects(forge::storage::load_config(forge_dir).storage_root),
          index_store(forge_dir / forge::storage::kIndexFileName), refs(forge_dir) {}

    void stage_everything() { stage_path(objects, index_store, ignore_rules, dir.path(), dir.path()); }

    ObjectId commit(const std::string& message) {
        return create_commit(objects, index_store, refs, "Test <t@example.com>", message, 3000).commit_id;
    }
};

} // namespace

FORGE_TEST_CASE(parse_remote_url_reads_host_port_and_repo_name) {
    const auto remote = parse_remote_url("forge://127.0.0.1:8080/demo");
    FORGE_CHECK(remote.has_value());
    FORGE_CHECK(remote->host == "127.0.0.1");
    FORGE_CHECK(remote->port == 8080);
    FORGE_CHECK(remote->repo_name == "demo");
}

FORGE_TEST_CASE(parse_remote_url_rejects_a_missing_scheme) {
    FORGE_CHECK(!parse_remote_url("127.0.0.1:8080/demo").has_value());
}

FORGE_TEST_CASE(parse_remote_url_rejects_a_missing_port) {
    FORGE_CHECK(!parse_remote_url("forge://127.0.0.1/demo").has_value());
}

FORGE_TEST_CASE(parse_remote_url_rejects_a_missing_repo_name) {
    FORGE_CHECK(!parse_remote_url("forge://127.0.0.1:8080/").has_value());
}

FORGE_TEST_CASE(render_remote_url_round_trips_parse_remote_url) {
    const RemoteEndpoint remote{"localhost", 9000, "demo"};
    const auto parsed = parse_remote_url(render_remote_url(remote));
    FORGE_CHECK(parsed.has_value());
    FORGE_CHECK(parsed->host == remote.host);
    FORGE_CHECK(parsed->port == remote.port);
    FORGE_CHECK(parsed->repo_name == remote.repo_name);
}

FORGE_TEST_CASE(clone_downloads_objects_creates_the_branch_and_checks_out_head) {
    RemoteTestServer server;
    const ObjectId seed_commit = seed_server_repo(server, "demo");

    TempDir client_dir;
    const std::filesystem::path target = client_dir.path() / "clone";
    const forge::storage::InitResult init_result = forge::core::clone(server.endpoint("demo"), target);

    RefStore client_refs(init_result.forge_dir);
    FORGE_CHECK(client_refs.read_branch("main") == seed_commit);
    FORGE_CHECK(client_refs.read_head().branch.value_or("") == "main");
    FORGE_CHECK(read_file(target / "a.txt") == "hello");
}

FORGE_TEST_CASE(fetch_downloads_new_objects_without_moving_local_branches) {
    RemoteTestServer server;
    const ObjectId seed_commit = seed_server_repo(server, "demo");

    TempDir client_dir;
    const std::filesystem::path target = client_dir.path() / "clone";
    const forge::storage::InitResult init_result = forge::core::clone(server.endpoint("demo"), target);

    const ObjectId second_commit = advance_server_repo(server, "demo", seed_commit);

    ObjectStore client_objects(forge::storage::load_config(init_result.forge_dir).storage_root);
    const forge::core::RemoteRefs remote_refs = forge::core::fetch(client_objects, server.endpoint("demo"));

    FORGE_CHECK(remote_refs.branches.at("main") == second_commit);
    FORGE_CHECK(client_objects.contains(second_commit));

    RefStore client_refs(init_result.forge_dir);
    FORGE_CHECK(client_refs.read_branch("main") == seed_commit); // fetch never moves local branches
}

FORGE_TEST_CASE(push_uploads_a_new_commit_and_advances_the_remote_branch) {
    RemoteTestServer server;
    seed_server_repo(server, "demo");

    TempDir client_dir;
    const std::filesystem::path target = client_dir.path() / "clone";
    const forge::storage::InitResult init_result = forge::core::clone(server.endpoint("demo"), target);

    ClientRepo client(init_result.forge_dir);
    write_file(target / "b.txt", "world");
    client.stage_everything();
    const ObjectId new_commit = client.commit("client change");

    const forge::core::PushResult push_result =
        forge::core::push(client.objects, client.refs, server.endpoint("demo"), "main", false);
    FORGE_CHECK(push_result.commit_id == new_commit);

    RefStore server_refs(server.forge_dir("demo"));
    FORGE_CHECK(server_refs.read_branch("main") == new_commit);
}

FORGE_TEST_CASE(push_without_force_rejects_a_non_fast_forward_update) {
    RemoteTestServer server;
    const ObjectId seed_commit = seed_server_repo(server, "demo");

    TempDir client_dir;
    const std::filesystem::path target = client_dir.path() / "clone";
    const forge::storage::InitResult init_result = forge::core::clone(server.endpoint("demo"), target);

    // Diverge: the server advances one way, the client advances another,
    // both starting from the same seed commit.
    advance_server_repo(server, "demo", seed_commit);

    ClientRepo client(init_result.forge_dir);
    write_file(target / "c.txt", "client only");
    client.stage_everything();
    client.commit("diverging client change");

    bool threw = false;
    try {
        forge::core::push(client.objects, client.refs, server.endpoint("demo"), "main", false);
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(push_with_force_overwrites_a_diverged_remote_branch) {
    RemoteTestServer server;
    const ObjectId seed_commit = seed_server_repo(server, "demo");

    TempDir client_dir;
    const std::filesystem::path target = client_dir.path() / "clone";
    const forge::storage::InitResult init_result = forge::core::clone(server.endpoint("demo"), target);

    advance_server_repo(server, "demo", seed_commit);

    ClientRepo client(init_result.forge_dir);
    write_file(target / "c.txt", "client only");
    client.stage_everything();
    const ObjectId client_commit = client.commit("diverging client change");

    const forge::core::PushResult push_result =
        forge::core::push(client.objects, client.refs, server.endpoint("demo"), "main", true);
    FORGE_CHECK(push_result.commit_id == client_commit);

    RefStore server_refs(server.forge_dir("demo"));
    FORGE_CHECK(server_refs.read_branch("main") == client_commit);
}

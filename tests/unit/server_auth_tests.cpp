#include <cstdint>
#include <thread>

#include "core/base64.hpp"
#include "core/object_encoding.hpp"
#include "domain/permissions.hpp"
#include "server/app.hpp"
#include "storage/auth_store.hpp"
#include "support/http_test_client.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"
#include "transport/http_server.hpp"

using forge::test::send_raw_http_request;
using forge::test::TempDir;
using forge::transport::HttpServer;

namespace {

struct RunningAuthServer {
    TempDir repos_root;
    TempDir data_root;
    HttpServer http_server{"127.0.0.1", 0};
    std::thread thread;

    RunningAuthServer() {
        forge::server::wire_routes(http_server, repos_root.path(), data_root.path());
        http_server.start();
        thread = std::thread([this] { http_server.serve(); });
    }

    ~RunningAuthServer() {
        http_server.stop();
        thread.join();
    }

    std::string send(const std::string& request) const { return send_raw_http_request(http_server.port(), request); }
};

std::string body_of(const std::string& raw_response) {
    const std::size_t blank_line = raw_response.find("\r\n\r\n");
    return blank_line == std::string::npos ? "" : raw_response.substr(blank_line + 4);
}

// Builds an SSH wire-format length-prefixed string field (RFC 4253
// §5.6) — real key blobs are a sequence of these; embedded NUL bytes in
// the length prefix rule out using const char* + operator+ (which would
// silently truncate at the first NUL), so this builds the std::string
// directly.
std::string ssh_string(const std::string& s) {
    std::string out;
    const std::uint32_t len = static_cast<std::uint32_t>(s.size());
    out.push_back(static_cast<char>((len >> 24) & 0xFF));
    out.push_back(static_cast<char>((len >> 16) & 0xFF));
    out.push_back(static_cast<char>((len >> 8) & 0xFF));
    out.push_back(static_cast<char>(len & 0xFF));
    out += s;
    return out;
}

std::string http_request(
    const std::string& method, const std::string& path, const std::string& body,
    const std::string& extra_header = "") {
    std::ostringstream out;
    out << method << ' ' << path << " HTTP/1.1\r\nHost: x\r\n";
    if (!extra_header.empty()) {
        out << extra_header << "\r\n";
    }
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return out.str();
}

} // namespace

FORGE_TEST_CASE(register_then_login_succeeds_with_the_right_password) {
    RunningAuthServer server;

    const std::string register_response = server.send(http_request("POST", "/users?username=alice", "hunter2"));
    FORGE_CHECK(register_response.find("HTTP/1.1 200 OK") == 0);

    const std::string login_response = server.send(http_request("POST", "/login?username=alice", "hunter2"));
    FORGE_CHECK(login_response.find("HTTP/1.1 200 OK") == 0);
    FORGE_CHECK(body_of(login_response).find("\"token\":\"") != std::string::npos);
}

FORGE_TEST_CASE(login_fails_with_the_wrong_password) {
    RunningAuthServer server;
    server.send(http_request("POST", "/users?username=alice", "hunter2"));

    const std::string response = server.send(http_request("POST", "/login?username=alice", "wrong-password"));
    FORGE_CHECK(response.find("HTTP/1.1 401") == 0);
}

FORGE_TEST_CASE(registering_the_same_username_twice_fails) {
    RunningAuthServer server;
    server.send(http_request("POST", "/users?username=alice", "hunter2"));
    const std::string second = server.send(http_request("POST", "/users?username=alice", "different"));
    FORGE_CHECK(second.find("HTTP/1.1 200") != 0);
}

FORGE_TEST_CASE(ssh_keys_endpoint_requires_a_bearer_token) {
    RunningAuthServer server;
    server.send(http_request("POST", "/users?username=alice", "hunter2"));

    const std::string blob = ssh_string("ssh-ed25519") + ssh_string(std::string(32, 'k'));
    const std::string key_line = "ssh-ed25519 " + forge::core::base64_encode(blob) + " laptop";

    const std::string without_auth = server.send(http_request("POST", "/ssh-keys", key_line));
    FORGE_CHECK(without_auth.find("HTTP/1.1 401") == 0);

    const std::string login = body_of(server.send(http_request("POST", "/login?username=alice", "hunter2")));
    const std::size_t token_start = login.find("\"token\":\"") + std::string("\"token\":\"").size();
    const std::string token = login.substr(token_start, login.find('"', token_start) - token_start);

    const std::string with_auth =
        server.send(http_request("POST", "/ssh-keys", key_line, "Authorization: Bearer " + token));
    FORGE_CHECK(with_auth.find("HTTP/1.1 200 OK") == 0);
    FORGE_CHECK(body_of(with_auth).find("SHA256:") != std::string::npos);
}

FORGE_TEST_CASE(pushing_to_a_repo_with_no_recorded_permissions_stays_open) {
    RunningAuthServer server;
    const std::string blob = forge::core::encode_canonical_object("blob", "hello");
    const std::string response = server.send(http_request("POST", "/object?repo=demo", blob));
    FORGE_CHECK(response.find("HTTP/1.1 200 OK") == 0); // legacy/open: no permission recorded yet
}

FORGE_TEST_CASE(pushing_to_a_repo_with_a_recorded_permission_requires_write_access) {
    RunningAuthServer server;
    server.send(http_request("POST", "/users?username=alice", "hunter2"));

    // Create the repo first (an anonymous upload, same as the "stays
    // open" case above), then lock it down by recording a permission —
    // done directly against AuthStore since bootstrapping the very
    // first grant on a repo has no existing Admin to make the call
    // through POST /permissions (see server/app.cpp's doc comment).
    const std::string blob = forge::core::encode_canonical_object("blob", "hello");
    server.send(http_request("POST", "/object?repo=demo", blob));
    forge::storage::AuthStore auth_store(server.data_root.path());
    auth_store.set_permission("demo", "alice", forge::domain::Role::Admin, 1000);

    const std::string blob2 = forge::core::encode_canonical_object("blob", "world");
    const std::string denied = server.send(http_request("POST", "/object?repo=demo", blob2));
    FORGE_CHECK(denied.find("HTTP/1.1 403") == 0);

    const std::string login = body_of(server.send(http_request("POST", "/login?username=alice", "hunter2")));
    const std::size_t token_start = login.find("\"token\":\"") + std::string("\"token\":\"").size();
    const std::string token = login.substr(token_start, login.find('"', token_start) - token_start);

    const std::string allowed =
        server.send(http_request("POST", "/object?repo=demo", blob2, "Authorization: Bearer " + token));
    FORGE_CHECK(allowed.find("HTTP/1.1 200 OK") == 0);
}

FORGE_TEST_CASE(permissions_endpoint_requires_admin_once_a_permission_exists) {
    RunningAuthServer server;
    server.send(http_request("POST", "/users?username=alice", "hunter2"));
    server.send(http_request("POST", "/users?username=bob", "hunter3"));

    const std::string blob = forge::core::encode_canonical_object("blob", "hello");
    server.send(http_request("POST", "/object?repo=demo", blob));
    forge::storage::AuthStore auth_store(server.data_root.path());
    auth_store.set_permission("demo", "alice", forge::domain::Role::Admin, 1000);

    const std::string bob_login = body_of(server.send(http_request("POST", "/login?username=bob", "hunter3")));
    const std::size_t bob_token_start = bob_login.find("\"token\":\"") + std::string("\"token\":\"").size();
    const std::string bob_token = bob_login.substr(bob_token_start, bob_login.find('"', bob_token_start) - bob_token_start);

    // Bob isn't an admin on "demo" — he can't grant himself access.
    const std::string denied = server.send(http_request(
        "POST", "/permissions?repo=demo&username=bob&role=write", "", "Authorization: Bearer " + bob_token));
    FORGE_CHECK(denied.find("HTTP/1.1 403") == 0);

    const std::string alice_login = body_of(server.send(http_request("POST", "/login?username=alice", "hunter2")));
    const std::size_t alice_token_start = alice_login.find("\"token\":\"") + std::string("\"token\":\"").size();
    const std::string alice_token =
        alice_login.substr(alice_token_start, alice_login.find('"', alice_token_start) - alice_token_start);

    const std::string granted = server.send(http_request(
        "POST", "/permissions?repo=demo&username=bob&role=write", "", "Authorization: Bearer " + alice_token));
    FORGE_CHECK(granted.find("HTTP/1.1 200 OK") == 0);
    FORGE_CHECK(auth_store.get_permission("demo", "bob") == forge::domain::Role::Write);
}

FORGE_TEST_CASE(login_endpoint_rate_limits_repeated_attempts_from_the_same_client) {
    RunningAuthServer server;
    server.send(http_request("POST", "/users?username=alice", "hunter2"));

    // The bucket (capacity 10, refilled at 1/sec — see server/app.cpp's
    // login_rate_limiter) starts full. Each attempt here costs a full
    // HTTP round trip plus a PBKDF2 hash (storage/auth_store.hpp), which
    // isn't instantaneous, so a fixed "the Nth call is the throttled
    // one" assertion would be timing-flaky; instead, fire well past
    // capacity and check throttling kicked in at least once.
    int unauthorized_count = 0;
    int throttled_count = 0;
    for (int i = 0; i < 30; ++i) {
        const std::string response = server.send(http_request("POST", "/login?username=alice", "wrong-password"));
        if (response.find("HTTP/1.1 401") == 0) {
            ++unauthorized_count;
        } else if (response.find("HTTP/1.1 429") == 0) {
            ++throttled_count;
        }
    }
    FORGE_CHECK(throttled_count > 0);
    FORGE_CHECK(unauthorized_count + throttled_count == 30);
}

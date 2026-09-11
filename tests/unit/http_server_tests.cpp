#include <thread>

#include "server/app.hpp"
#include "support/http_test_client.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"
#include "transport/http_server.hpp"

using forge::transport::HttpRequest;
using forge::transport::HttpServer;
using forge::transport::plain_text_response;
using forge::test::send_raw_http_request;

namespace {

// Starts `server` listening on an OS-assigned port and runs its accept
// loop on a background thread, joined on destruction — so a test just
// declares one of these and the server is guaranteed listening (start()
// is synchronous) before the test sends its first request.
struct RunningServer {
    HttpServer& server;
    std::thread thread;

    explicit RunningServer(HttpServer& s) : server(s) {
        server.start();
        thread = std::thread([this] { server.serve(); });
    }

    ~RunningServer() {
        server.stop();
        thread.join();
    }
};

} // namespace

FORGE_TEST_CASE(http_server_routes_a_registered_get_request) {
    HttpServer server("127.0.0.1", 0);
    server.route("GET", "/ping", [](const HttpRequest&) { return plain_text_response(200, "OK", "pong"); });
    RunningServer running(server);

    const std::string response = send_raw_http_request(server.port(), "GET /ping HTTP/1.1\r\nHost: x\r\n\r\n");
    FORGE_CHECK(response.find("HTTP/1.1 200 OK") == 0);
    FORGE_CHECK(response.find("pong") != std::string::npos);
}

FORGE_TEST_CASE(http_server_returns_404_for_an_unregistered_path) {
    HttpServer server("127.0.0.1", 0);
    server.route("GET", "/known", [](const HttpRequest&) { return plain_text_response(200, "OK", ""); });
    RunningServer running(server);

    const std::string response = send_raw_http_request(server.port(), "GET /unknown HTTP/1.1\r\nHost: x\r\n\r\n");
    FORGE_CHECK(response.find("HTTP/1.1 404") == 0);
}

FORGE_TEST_CASE(http_server_captures_the_client_remote_address) {
    HttpServer server("127.0.0.1", 0);
    std::string captured;
    server.route("GET", "/whoami", [&captured](const HttpRequest& request) {
        captured = request.remote_address;
        return plain_text_response(200, "OK", "");
    });
    RunningServer running(server);

    send_raw_http_request(server.port(), "GET /whoami HTTP/1.1\r\nHost: x\r\n\r\n");
    FORGE_CHECK(captured == "127.0.0.1");
}

FORGE_TEST_CASE(http_server_matches_method_and_path_together) {
    HttpServer server("127.0.0.1", 0);
    server.route("GET", "/thing", [](const HttpRequest&) { return plain_text_response(200, "OK", "got"); });
    RunningServer running(server);

    // Same path, different method: should not match the GET-only route.
    const std::string response = send_raw_http_request(server.port(), "POST /thing HTTP/1.1\r\nHost: x\r\n\r\n");
    FORGE_CHECK(response.find("HTTP/1.1 404") == 0);
}

FORGE_TEST_CASE(http_server_passes_the_request_body_to_the_handler) {
    HttpServer server("127.0.0.1", 0);
    server.route("POST", "/echo", [](const HttpRequest& request) {
        return plain_text_response(200, "OK", request.body);
    });
    RunningServer running(server);

    const std::string request =
        "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 11\r\n\r\nhello world";
    const std::string response = send_raw_http_request(server.port(), request);
    FORGE_CHECK(response.find("hello world") != std::string::npos);
}

FORGE_TEST_CASE(http_server_rejects_a_malformed_request_line) {
    HttpServer server("127.0.0.1", 0);
    RunningServer running(server);

    const std::string response = send_raw_http_request(server.port(), "not a valid http request\r\n\r\n");
    FORGE_CHECK(response.find("HTTP/1.1 400") == 0);
}

FORGE_TEST_CASE(wire_routes_exposes_a_healthz_endpoint) {
    forge::test::TempDir repos_root;
    forge::test::TempDir data_root;
    HttpServer server("127.0.0.1", 0);
    forge::server::wire_routes(server, repos_root.path(), data_root.path());
    RunningServer running(server);

    const std::string response = send_raw_http_request(server.port(), "GET /healthz HTTP/1.1\r\nHost: x\r\n\r\n");
    FORGE_CHECK(response.find("HTTP/1.1 200 OK") == 0);
    FORGE_CHECK(response.find(R"("status":"ok")") != std::string::npos);
}

FORGE_TEST_CASE(wire_routes_exposes_a_version_endpoint) {
    forge::test::TempDir repos_root;
    forge::test::TempDir data_root;
    HttpServer server("127.0.0.1", 0);
    forge::server::wire_routes(server, repos_root.path(), data_root.path());
    RunningServer running(server);

    const std::string response = send_raw_http_request(server.port(), "GET /version HTTP/1.1\r\nHost: x\r\n\r\n");
    FORGE_CHECK(response.find("HTTP/1.1 200 OK") == 0);
    FORGE_CHECK(response.find("version") != std::string::npos);
}

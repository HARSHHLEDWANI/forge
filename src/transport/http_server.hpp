#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "transport/http_message.hpp"
#include "transport/socket_compat.hpp"

namespace forge::transport {

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

// A minimal HTTP/1.1 server: one request per connection, no keep-alive —
// a deliberate V1 simplification (same spirit as diff.hpp's single-hunk
// unified diff): correct and easy to verify over protocol-completeness
// the project doesn't need yet, and can be revisited if it's ever
// actually missed. Routed to exact (method, path) handlers registered
// via route() — no wildcards/path parameters, since V2's route set
// (health check now, repository endpoints in Phase 13) is small and
// fixed, not a general web framework.
//
// Single-threaded accept loop: correct before fast (frozen-scope.md);
// concurrent request handling is explicitly deferred to V5 ("only after
// measurement"), not a V2 server correctness requirement.
class HttpServer {
public:
    HttpServer(std::string bind_address, std::uint16_t port);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Registers a handler for an exact (method, path) pair, checked in
    // registration order. Must be called before start().
    void route(std::string method, std::string path, HttpHandler handler);

    // Binds and starts listening. Throws core::ForgeError on failure.
    // Split from serve() so a caller — or a test spinning the server up
    // on a background thread — can be sure it's actually listening
    // before a client tries to connect, rather than racing serve()'s
    // internal bind. If constructed with port 0, the OS assigns a free
    // port; call port() afterward to find out which one.
    void start();

    // Accepts and serves connections until stop() is called from
    // another thread. Must be called after start(); blocks the calling
    // thread, so tests run it on a background std::thread.
    void serve();

    // Thread-safe. Makes an in-progress serve() loop return once its
    // current accept-wait (bounded by a short poll interval) elapses.
    void stop();

    std::uint16_t port() const { return port_; }

private:
    HttpResponse dispatch(const HttpRequest& request) const;
    void handle_connection(ForgeSocket client, std::string remote_address) const;

    std::string bind_address_;
    std::uint16_t port_;
    ForgeSocket listen_socket_ = kInvalidForgeSocket;
    std::atomic<bool> stop_requested_{false};
    std::vector<std::pair<std::pair<std::string, std::string>, HttpHandler>> routes_;
};

} // namespace forge::transport

#include "transport/http_server.hpp"

#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using ForgeSockLen = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
using ForgeSockLen = socklen_t;
#endif

#include "core/error.hpp"
#include "transport/http_parser.hpp"

namespace forge::transport {

namespace {

constexpr std::size_t kMaxHeadSize = 64 * 1024;       // guards against an unbounded header from a broken/hostile client
constexpr std::size_t kMaxBodySize = 10 * 1024 * 1024; // Phase 12's API bodies are small JSON; raised when Phase 13 needs to stream objects
constexpr int kListenBacklog = 16;
constexpr long kPollTimeoutMicros = 200'000; // how promptly stop() is noticed

void send_all(ForgeSocket client, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto result =
            send(client, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (result <= 0) {
            return; // client gone: nothing more we can do
        }
        sent += static_cast<std::size_t>(result);
    }
}

void send_response(ForgeSocket client, const HttpResponse& response) {
    send_all(client, render_response(response));
}

} // namespace

HttpServer::HttpServer(std::string bind_address, std::uint16_t port)
    : bind_address_(std::move(bind_address)), port_(port) {}

HttpServer::~HttpServer() {
    close_socket(listen_socket_);
    socket_platform_cleanup();
}

void HttpServer::route(std::string method, std::string path, HttpHandler handler) {
    routes_.emplace_back(std::make_pair(std::move(method), std::move(path)), std::move(handler));
}

void HttpServer::start() {
    socket_platform_init();

    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_ == kInvalidForgeSocket) {
        throw core::ForgeError("failed to create listening socket: " + last_socket_error_message());
    }

    const int reuse = 1;
    setsockopt(
        listen_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (bind_address_.empty() || bind_address_ == "0.0.0.0") {
        address.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, bind_address_.c_str(), &address.sin_addr) != 1) {
        throw core::ForgeError("invalid bind address: " + bind_address_);
    }

    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        throw core::ForgeError("failed to bind to " + bind_address_ + ":" + std::to_string(port_) + ": " +
                                last_socket_error_message());
    }
    if (listen(listen_socket_, kListenBacklog) != 0) {
        throw core::ForgeError("failed to listen: " + last_socket_error_message());
    }

    if (port_ == 0) {
        sockaddr_in bound{};
        ForgeSockLen bound_len = sizeof(bound);
        if (getsockname(listen_socket_, reinterpret_cast<sockaddr*>(&bound), &bound_len) == 0) {
            port_ = ntohs(bound.sin_port);
        }
    }
}

void HttpServer::serve() {
    while (!stop_requested_.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_socket_, &read_fds);
        timeval timeout{0, kPollTimeoutMicros};

        const int ready =
            select(static_cast<int>(listen_socket_) + 1, &read_fds, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue; // timed out (expected, just re-checking stop_requested_) or a transient error
        }

        const ForgeSocket client = accept(listen_socket_, nullptr, nullptr);
        if (client == kInvalidForgeSocket) {
            continue;
        }
        handle_connection(client);
        close_socket(client);
    }
}

void HttpServer::stop() { stop_requested_.store(true); }

void HttpServer::handle_connection(ForgeSocket client) const {
    std::string buffer;
    char chunk[4096];
    std::size_t header_end = std::string::npos;

    while (header_end == std::string::npos) {
        const auto received = recv(client, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return; // client disconnected before sending a complete request
        }
        buffer.append(chunk, static_cast<std::size_t>(received));
        if (buffer.size() > kMaxHeadSize) {
            send_response(client, plain_text_response(431, "Request Header Fields Too Large", ""));
            return;
        }
        header_end = buffer.find("\r\n\r\n");
    }

    const std::optional<HttpRequest> parsed = parse_request_head(buffer.substr(0, header_end));
    if (!parsed) {
        send_response(client, plain_text_response(400, "Bad Request", ""));
        return;
    }
    HttpRequest request = *parsed;
    request.body = buffer.substr(header_end + 4); // body bytes already read as part of the initial recv burst

    std::size_t content_length = 0;
    if (const std::optional<std::string> length_header = find_header(request.headers, "content-length")) {
        try {
            const unsigned long long parsed_length = std::stoull(*length_header);
            content_length = static_cast<std::size_t>(parsed_length);
        } catch (const std::exception&) {
            send_response(client, plain_text_response(400, "Bad Request", ""));
            return;
        }
    }
    if (content_length > kMaxBodySize) {
        send_response(client, plain_text_response(413, "Payload Too Large", ""));
        return;
    }

    while (request.body.size() < content_length) {
        const auto received = recv(client, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            break; // client hung up mid-body: serve whatever actually arrived
        }
        request.body.append(chunk, static_cast<std::size_t>(received));
    }
    if (request.body.size() > content_length) {
        // Extra bytes belong to a pipelined next request, which this
        // one-request-per-connection server doesn't support.
        request.body.resize(content_length);
    }

    send_response(client, dispatch(request));
}

HttpResponse HttpServer::dispatch(const HttpRequest& request) const {
    for (const auto& [key, handler] : routes_) {
        if (key.first == request.method && key.second == request.path) {
            return handler(request);
        }
    }
    return plain_text_response(404, "Not Found", "not found\n");
}

} // namespace forge::transport

#include "transport/http_client.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <sstream>

#include "core/error.hpp"
#include "transport/http_parser.hpp"
#include "transport/socket_compat.hpp"

namespace forge::transport {

namespace {

std::string render_request(const HttpClientRequest& request) {
    std::ostringstream out;
    out << request.method << ' ' << request.path << " HTTP/1.1\r\n";
    for (const auto& [name, value] : request.headers) {
        if (name == "content-length" || name == "connection") {
            continue; // set below, always, regardless of what the caller put there
        }
        out << name << ": " << value << "\r\n";
    }
    out << "content-length: " << request.body.size() << "\r\n";
    out << "connection: close\r\n";
    out << "\r\n";
    out << request.body;
    return out.str();
}

struct AddrInfoGuard {
    addrinfo* info = nullptr;
    ~AddrInfoGuard() {
        if (info != nullptr) {
            freeaddrinfo(info);
        }
    }
};

} // namespace

HttpResponse send_http_request(std::string_view host, std::uint16_t port, const HttpClientRequest& request) {
    socket_platform_init();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    AddrInfoGuard resolved;
    const int gai_result =
        getaddrinfo(std::string(host).c_str(), std::to_string(port).c_str(), &hints, &resolved.info);
    if (gai_result != 0 || resolved.info == nullptr) {
        socket_platform_cleanup();
        throw core::ForgeError("failed to resolve host: " + std::string(host));
    }

    const ForgeSocket sock = socket(resolved.info->ai_family, resolved.info->ai_socktype, resolved.info->ai_protocol);
    if (sock == kInvalidForgeSocket) {
        socket_platform_cleanup();
        throw core::ForgeError("failed to create socket: " + last_socket_error_message());
    }

    if (connect(sock, resolved.info->ai_addr, static_cast<int>(resolved.info->ai_addrlen)) != 0) {
        close_socket(sock);
        socket_platform_cleanup();
        throw core::ForgeError(
            "failed to connect to " + std::string(host) + ":" + std::to_string(port) + ": " +
            last_socket_error_message());
    }

    const std::string raw_request = render_request(request);
    std::size_t sent = 0;
    while (sent < raw_request.size()) {
        const auto result = send(sock, raw_request.data() + sent, static_cast<int>(raw_request.size() - sent), 0);
        if (result <= 0) {
            close_socket(sock);
            socket_platform_cleanup();
            throw core::ForgeError("failed to send request: " + last_socket_error_message());
        }
        sent += static_cast<std::size_t>(result);
    }

    std::string raw_response;
    char chunk[4096];
    while (true) {
        const auto received = recv(sock, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            break;
        }
        raw_response.append(chunk, static_cast<std::size_t>(received));
    }
    close_socket(sock);
    socket_platform_cleanup();

    const std::size_t header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw core::ForgeError("malformed HTTP response from " + std::string(host) + ":" + std::to_string(port));
    }
    std::optional<HttpResponse> parsed = parse_response_head(raw_response.substr(0, header_end));
    if (!parsed) {
        throw core::ForgeError("malformed HTTP response from " + std::string(host) + ":" + std::to_string(port));
    }
    parsed->body = raw_response.substr(header_end + 4);
    return *parsed;
}

} // namespace forge::transport

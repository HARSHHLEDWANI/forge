#pragma once

// Minimal blocking HTTP client, test-only: connects to 127.0.0.1:port,
// sends a raw request verbatim, and returns everything the server sends
// back until it closes the connection. HttpServer under test never does
// keep-alive (see http_server.hpp), so "until close" is exactly "the
// whole response" — no separate response parser needed here, tests just
// assert on substrings of the raw bytes.

#include <cstdint>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "core/error.hpp"
#include "transport/socket_compat.hpp"

namespace forge::test {

inline std::string send_raw_http_request(std::uint16_t port, const std::string& request) {
    forge::transport::socket_platform_init();

    const ForgeSocket sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidForgeSocket) {
        forge::transport::socket_platform_cleanup();
        throw forge::core::ForgeError("test client: failed to create socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        forge::transport::close_socket(sock);
        forge::transport::socket_platform_cleanup();
        throw forge::core::ForgeError("test client: failed to connect to 127.0.0.1:" + std::to_string(port));
    }

    std::size_t sent = 0;
    while (sent < request.size()) {
        const auto result = send(sock, request.data() + sent, static_cast<int>(request.size() - sent), 0);
        if (result <= 0) {
            break;
        }
        sent += static_cast<std::size_t>(result);
    }

    std::string response;
    char chunk[4096];
    while (true) {
        const auto received = recv(sock, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            break;
        }
        response.append(chunk, static_cast<std::size_t>(received));
    }

    forge::transport::close_socket(sock);
    forge::transport::socket_platform_cleanup();
    return response;
}

} // namespace forge::test

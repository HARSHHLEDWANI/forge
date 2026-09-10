#include "transport/socket_compat.hpp"

#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

#include "core/error.hpp"

namespace forge::transport {

void socket_platform_init() {
#if defined(_WIN32)
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        throw core::ForgeError("failed to initialize Winsock");
    }
#endif
}

void socket_platform_cleanup() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

void close_socket(ForgeSocket socket) noexcept {
    if (socket == kInvalidForgeSocket) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

std::string last_socket_error_message() {
#if defined(_WIN32)
    return "winsock error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

} // namespace forge::transport

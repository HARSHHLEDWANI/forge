#pragma once

#include <cstdint>
#include <string>

// Thin cross-platform socket layer: BSD sockets on POSIX, Winsock on
// Windows. Only the handful of calls http_server.cpp actually needs —
// this is not a general sockets wrapper, just enough surface to hide
// `SOCKET` vs `int` and `closesocket` vs `close` behind one name each,
// the same spirit as atomic_file.cpp's per-platform fsync branches.

#if defined(_WIN32)
#include <winsock2.h>
using ForgeSocket = SOCKET;
inline constexpr ForgeSocket kInvalidForgeSocket = INVALID_SOCKET;
#else
using ForgeSocket = int;
inline constexpr ForgeSocket kInvalidForgeSocket = -1;
#endif

namespace forge::transport {

// Initializes the platform socket library (Winsock's WSAStartup; a no-op
// on POSIX). Must be called once before any socket use and matched by
// socket_platform_cleanup(). Throws core::ForgeError on failure.
void socket_platform_init();
void socket_platform_cleanup();

void close_socket(ForgeSocket socket) noexcept;

// The platform's own description of the last socket error (errno's
// strerror on POSIX, WSAGetLastError's FormatMessage on Windows) — used
// to make a thrown core::ForgeError actually say what went wrong instead
// of just "it failed".
std::string last_socket_error_message();

} // namespace forge::transport

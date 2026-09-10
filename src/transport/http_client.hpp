#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "transport/http_message.hpp"

namespace forge::transport {

struct HttpClientRequest {
    std::string method;
    std::string path; // must start with '/'; include the query string, if any
    HttpHeaders headers;
    std::string body;
};

// One-shot blocking client, no connection reuse — matches the server's
// own no-keep-alive stance (see http_server.hpp): resolve, connect, send
// one request, read until the peer closes (exactly the whole response,
// since neither side of this protocol ever keeps a connection open),
// parse, disconnect. Throws core::ForgeError on any transport failure
// (resolution/connect/send/recv) or if the response can't be parsed as
// HTTP — see core/remote.hpp for the caller that turns this into
// clone/fetch/push.
HttpResponse send_http_request(std::string_view host, std::uint16_t port, const HttpClientRequest& request);

} // namespace forge::transport

#pragma once

#include <map>
#include <string>

namespace forge::transport {

// Header names are stored lower-cased (HTTP header names are
// case-insensitive per RFC 7230 §3.2) so a lookup never has to guess
// what case a client or handler used; std::map (not unordered_map) so
// rendered responses have a deterministic header order, which makes
// tests easy to write exact-match assertions against.
using HttpHeaders = std::map<std::string, std::string>;

struct HttpRequest {
    std::string method;      // "GET", "POST", ... verbatim as sent
    std::string path;        // percent-decoded is out of scope for V1: raw path component, no query string
    std::string query;       // raw query string after '?', "" if none
    std::string version;     // "HTTP/1.1" as sent
    HttpHeaders headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string status_text = "OK";
    HttpHeaders headers;
    std::string body;
};

// Convenience constructors covering the response shapes the server
// actually needs (see server/app.cpp) — JSON success/error bodies with
// Content-Type and Content-Length set correctly, so a handler never has
// to remember to do that itself.
HttpResponse json_response(int status, std::string status_text, std::string json_body);
HttpResponse plain_text_response(int status, std::string status_text, std::string text_body);

// Renders `response` as the bytes to write to the socket: status line,
// headers (Content-Length is added/overwritten to match the actual body
// size, and "Connection: close" is always set — see http_server.hpp's
// doc comment on why V1 doesn't do keep-alive), a blank line, then the
// body.
std::string render_response(const HttpResponse& response);

} // namespace forge::transport

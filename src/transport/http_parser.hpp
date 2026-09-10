#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "transport/http_message.hpp"

namespace forge::transport {

// Parses the request line and headers only — everything up to but not
// including the blank line that ends the header block. The caller
// (http_server.cpp) reads the body separately once it knows
// Content-Length, since that means going back to the socket again.
//
// Lenient about line endings (bare "\n" as well as "\r\n") since nothing
// here needs strict RFC 7230 compliance and a lenient parser is friendlier
// to whatever writes the request, including this project's own test
// client code. Returns nullopt for anything genuinely malformed: an
// empty/absent request line, a request line without both a path and a
// version, or a header line missing ':'.
std::optional<HttpRequest> parse_request_head(std::string_view raw_head);

// Case-insensitive header lookup (HttpHeaders always stores lower-cased
// names — see http_message.hpp); `lower_name` must already be lower-cased
// by the caller.
std::optional<std::string> find_header(const HttpHeaders& headers, std::string_view lower_name);

// Parses a "key=value&key2=value2" query string (HttpRequest::query, with
// no leading '?') into a name -> value map. No percent-decoding: every
// caller in this codebase only ever puts repo names, hex object ids, and
// branch names in a query string, and those already reject the
// characters percent-encoding exists to escape (see
// server/repo_registry.hpp, branching.hpp). A bare "key" with no '='
// maps to an empty value; a repeated key keeps its last occurrence.
std::map<std::string, std::string> parse_query_string(std::string_view query);

// Renders `params` back into a "key=value&key2=value2" query string —
// the client-side inverse of parse_query_string, used to build request
// URLs (see core/remote.hpp).
std::string render_query_string(const std::map<std::string, std::string>& params);

// Parses an HTTP response's status line and headers, symmetric to
// parse_request_head — everything up to but not including the blank
// line that ends the header block; the body is the caller's problem
// (see transport/http_client.hpp). Returns nullopt for a missing/empty
// status line, a non-numeric status code, or a header line missing ':'.
std::optional<HttpResponse> parse_response_head(std::string_view raw_head);

} // namespace forge::transport

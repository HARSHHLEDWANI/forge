#include "support/test_framework.hpp"
#include "transport/http_parser.hpp"

using forge::transport::find_header;
using forge::transport::HttpRequest;
using forge::transport::parse_request_head;

FORGE_TEST_CASE(parse_request_head_reads_method_path_and_version) {
    const auto request = parse_request_head("GET /healthz HTTP/1.1\r\nHost: localhost\r\n");
    FORGE_CHECK(request.has_value());
    FORGE_CHECK(request->method == "GET");
    FORGE_CHECK(request->path == "/healthz");
    FORGE_CHECK(request->version == "HTTP/1.1");
}

FORGE_TEST_CASE(parse_request_head_splits_query_string_from_path) {
    const auto request = parse_request_head("GET /repos?name=x HTTP/1.1\r\n");
    FORGE_CHECK(request.has_value());
    FORGE_CHECK(request->path == "/repos");
    FORGE_CHECK(request->query == "name=x");
}

FORGE_TEST_CASE(parse_request_head_lower_cases_header_names) {
    const auto request = parse_request_head("GET / HTTP/1.1\r\nContent-Type: application/json\r\n");
    FORGE_CHECK(request.has_value());
    FORGE_CHECK(find_header(request->headers, "content-type").value_or("") == "application/json");
}

FORGE_TEST_CASE(parse_request_head_trims_header_value_whitespace) {
    const auto request = parse_request_head("GET / HTTP/1.1\r\nX-Custom:   spaced out   \r\n");
    FORGE_CHECK(request.has_value());
    FORGE_CHECK(find_header(request->headers, "x-custom").value_or("") == "spaced out");
}

FORGE_TEST_CASE(parse_request_head_tolerates_bare_newlines) {
    const auto request = parse_request_head("GET / HTTP/1.1\nHost: localhost\n");
    FORGE_CHECK(request.has_value());
    FORGE_CHECK(request->method == "GET");
}

FORGE_TEST_CASE(parse_request_head_rejects_empty_input) {
    FORGE_CHECK(!parse_request_head("").has_value());
}

FORGE_TEST_CASE(parse_request_head_rejects_a_request_line_missing_the_version) {
    FORGE_CHECK(!parse_request_head("GET /only-two-tokens\r\n").has_value());
}

FORGE_TEST_CASE(parse_request_head_rejects_a_header_line_without_a_colon) {
    FORGE_CHECK(!parse_request_head("GET / HTTP/1.1\r\nnot-a-header-line\r\n").has_value());
}

FORGE_TEST_CASE(find_header_returns_nullopt_for_an_absent_header) {
    const auto request = parse_request_head("GET / HTTP/1.1\r\n");
    FORGE_CHECK(request.has_value());
    FORGE_CHECK(!find_header(request->headers, "authorization").has_value());
}

FORGE_TEST_CASE(parse_query_string_reads_key_value_pairs) {
    const auto params = forge::transport::parse_query_string("repo=x&branch=main");
    FORGE_CHECK(params.at("repo") == "x");
    FORGE_CHECK(params.at("branch") == "main");
}

FORGE_TEST_CASE(parse_query_string_handles_a_bare_key_with_no_value) {
    const auto params = forge::transport::parse_query_string("flag");
    FORGE_CHECK(params.at("flag").empty());
}

FORGE_TEST_CASE(parse_query_string_of_empty_input_is_empty) {
    FORGE_CHECK(forge::transport::parse_query_string("").empty());
}

FORGE_TEST_CASE(render_query_string_round_trips_parse_query_string) {
    const std::map<std::string, std::string> params{{"branch", "main"}, {"repo", "x"}};
    const std::string rendered = forge::transport::render_query_string(params);
    FORGE_CHECK(forge::transport::parse_query_string(rendered) == params);
}

FORGE_TEST_CASE(parse_response_head_reads_status_and_text) {
    const auto response = forge::transport::parse_response_head("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n");
    FORGE_CHECK(response.has_value());
    FORGE_CHECK(response->status == 404);
    FORGE_CHECK(response->status_text == "Not Found");
    FORGE_CHECK(find_header(response->headers, "content-length").value_or("") == "0");
}

FORGE_TEST_CASE(parse_response_head_rejects_a_non_numeric_status) {
    FORGE_CHECK(!forge::transport::parse_response_head("HTTP/1.1 oops Not A Status\r\n").has_value());
}

#include "support/test_framework.hpp"
#include "transport/http_message.hpp"

using forge::transport::HttpResponse;
using forge::transport::json_response;
using forge::transport::plain_text_response;
using forge::transport::render_response;

FORGE_TEST_CASE(render_response_includes_status_line_and_body) {
    HttpResponse response = plain_text_response(200, "OK", "hello");
    const std::string rendered = render_response(response);
    FORGE_CHECK(rendered.find("HTTP/1.1 200 OK\r\n") == 0);
    FORGE_CHECK(rendered.find("hello") != std::string::npos);
}

FORGE_TEST_CASE(render_response_sets_content_length_to_actual_body_size) {
    const HttpResponse response = plain_text_response(200, "OK", "12345");
    const std::string rendered = render_response(response);
    FORGE_CHECK(rendered.find("content-length: 5\r\n") != std::string::npos);
}

FORGE_TEST_CASE(render_response_always_sets_connection_close) {
    HttpResponse response = plain_text_response(200, "OK", "");
    response.headers["connection"] = "keep-alive"; // a handler's attempt to override
    const std::string rendered = render_response(response);
    FORGE_CHECK(rendered.find("connection: close\r\n") != std::string::npos);
    FORGE_CHECK(rendered.find("keep-alive") == std::string::npos);
}

FORGE_TEST_CASE(render_response_separates_headers_from_body_with_blank_line) {
    const HttpResponse response = plain_text_response(404, "Not Found", "missing\n");
    const std::string rendered = render_response(response);
    const std::size_t blank_line = rendered.find("\r\n\r\n");
    FORGE_CHECK(blank_line != std::string::npos);
    FORGE_CHECK(rendered.substr(blank_line + 4) == "missing\n");
}

FORGE_TEST_CASE(json_response_sets_json_content_type) {
    const HttpResponse response = json_response(200, "OK", R"({"a":1})");
    FORGE_CHECK(response.headers.at("content-type") == "application/json");
    FORGE_CHECK(response.body == R"({"a":1})");
}

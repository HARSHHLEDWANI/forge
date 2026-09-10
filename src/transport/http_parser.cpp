#include "transport/http_parser.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace forge::transport {

namespace {

std::string to_lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string_view> split_lines(std::string_view raw_head) {
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos <= raw_head.size()) {
        const std::size_t newline = raw_head.find('\n', pos);
        std::string_view line =
            newline == std::string_view::npos ? raw_head.substr(pos) : raw_head.substr(pos, newline - pos);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (newline == std::string_view::npos) {
            break;
        }
        pos = newline + 1;
    }
    return lines;
}

} // namespace

std::optional<HttpRequest> parse_request_head(std::string_view raw_head) {
    const std::vector<std::string_view> lines = split_lines(raw_head);
    if (lines.empty() || lines.front().empty()) {
        return std::nullopt;
    }

    const std::string_view request_line = lines.front();
    const std::size_t first_space = request_line.find(' ');
    if (first_space == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second_space = request_line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos) {
        return std::nullopt;
    }

    HttpRequest request;
    request.method = std::string(request_line.substr(0, first_space));
    const std::string_view target = request_line.substr(first_space + 1, second_space - first_space - 1);
    request.version = std::string(trim(request_line.substr(second_space + 1)));

    const std::size_t query_pos = target.find('?');
    if (query_pos == std::string_view::npos) {
        request.path = std::string(target);
    } else {
        request.path = std::string(target.substr(0, query_pos));
        request.query = std::string(target.substr(query_pos + 1));
    }
    if (request.path.empty() || request.method.empty() || request.version.empty()) {
        return std::nullopt;
    }
    // A well-formed request line has exactly three space-separated
    // tokens; a genuine HTTP version never contains a space or embedded
    // control character itself, so anything left over here means the
    // "request line" was actually something else entirely (e.g. a
    // client sending plain prose instead of HTTP).
    if (request.version.find(' ') != std::string::npos || request.version.rfind("HTTP/", 0) != 0) {
        return std::nullopt;
    }

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string_view line = trim(lines[i]);
        if (line.empty()) {
            continue; // a trailing blank line, if the caller included one
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            return std::nullopt;
        }
        request.headers[to_lower(trim(line.substr(0, colon)))] = std::string(trim(line.substr(colon + 1)));
    }

    return request;
}

std::optional<std::string> find_header(const HttpHeaders& headers, std::string_view lower_name) {
    const auto it = headers.find(std::string(lower_name));
    if (it == headers.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::map<std::string, std::string> parse_query_string(std::string_view query) {
    std::map<std::string, std::string> params;
    std::size_t pos = 0;
    while (pos <= query.size()) {
        const std::size_t amp = query.find('&', pos);
        const std::string_view pair = amp == std::string_view::npos ? query.substr(pos) : query.substr(pos, amp - pos);
        if (!pair.empty()) {
            const std::size_t eq = pair.find('=');
            if (eq == std::string_view::npos) {
                params[std::string(pair)] = "";
            } else {
                params[std::string(pair.substr(0, eq))] = std::string(pair.substr(eq + 1));
            }
        }
        if (amp == std::string_view::npos) {
            break;
        }
        pos = amp + 1;
    }
    return params;
}

std::string render_query_string(const std::map<std::string, std::string>& params) {
    std::string result;
    for (const auto& [key, value] : params) {
        if (!result.empty()) {
            result += '&';
        }
        result += key;
        result += '=';
        result += value;
    }
    return result;
}

std::optional<HttpResponse> parse_response_head(std::string_view raw_head) {
    const std::vector<std::string_view> lines = split_lines(raw_head);
    if (lines.empty() || lines.front().empty()) {
        return std::nullopt;
    }

    const std::string_view status_line = lines.front();
    const std::size_t first_space = status_line.find(' ');
    if (first_space == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second_space = status_line.find(' ', first_space + 1);
    const std::string_view status_code_str = second_space == std::string_view::npos
                                                  ? status_line.substr(first_space + 1)
                                                  : status_line.substr(first_space + 1, second_space - first_space - 1);

    HttpResponse response;
    try {
        response.status = std::stoi(std::string(status_code_str));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    response.status_text =
        second_space == std::string_view::npos ? "" : std::string(trim(status_line.substr(second_space + 1)));

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string_view line = trim(lines[i]);
        if (line.empty()) {
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            return std::nullopt;
        }
        response.headers[to_lower(trim(line.substr(0, colon)))] = std::string(trim(line.substr(colon + 1)));
    }

    return response;
}

} // namespace forge::transport

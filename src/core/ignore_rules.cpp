#include "core/ignore_rules.hpp"

namespace forge::core {

namespace {

std::vector<std::string_view> split(std::string_view text, char delim) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(delim, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

// Small recursive glob matcher for a single path segment: '*' matches any
// run of characters, '?' matches exactly one. Neither crosses a '/'
// because callers only ever pass one segment at a time.
bool glob_match_segment(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) {
        return text.empty();
    }
    if (pattern.front() == '*') {
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (glob_match_segment(pattern.substr(1), text.substr(i))) {
                return true;
            }
        }
        return false;
    }
    if (text.empty()) {
        return false;
    }
    if (pattern.front() == '?' || pattern.front() == text.front()) {
        return glob_match_segment(pattern.substr(1), text.substr(1));
    }
    return false;
}

} // namespace

bool IgnoreRules::Pattern::matches(std::string_view path) const {
    const std::vector<std::string_view> path_segments = split(path, '/');
    const std::vector<std::string_view> pattern_segments = split(text, '/');

    if (!anchored && pattern_segments.size() == 1) {
        return glob_match_segment(pattern_segments.front(), path_segments.back());
    }

    if (path_segments.size() != pattern_segments.size()) {
        return false;
    }
    for (std::size_t i = 0; i < path_segments.size(); ++i) {
        if (!glob_match_segment(pattern_segments[i], path_segments[i])) {
            return false;
        }
    }
    return true;
}

IgnoreRules::IgnoreRules(std::vector<Pattern> patterns) : patterns_(std::move(patterns)) {}

IgnoreRules IgnoreRules::parse(std::string_view forgeignore_content) {
    std::vector<Pattern> patterns;

    for (std::string_view line : split(forgeignore_content, '\n')) {
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1); // tolerate CRLF-authored .forgeignore files
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }

        bool anchored = false;
        if (line.front() == '/') {
            anchored = true;
            line.remove_prefix(1);
        }

        bool directory_only = false;
        if (!line.empty() && line.back() == '/') {
            directory_only = true;
            line.remove_suffix(1);
        }

        if (line.empty()) {
            continue; // pattern was just "/"
        }

        // An internal slash also anchors the pattern to the root, per
        // gitignore semantics (only a bare, slash-free pattern matches at
        // any depth).
        if (line.find('/') != std::string_view::npos) {
            anchored = true;
        }

        patterns.push_back(Pattern{std::string(line), anchored, directory_only});
    }

    return IgnoreRules(std::move(patterns));
}

bool IgnoreRules::is_ignored(std::string_view path, bool is_directory) const {
    for (const Pattern& pattern : patterns_) {
        if (pattern.directory_only && !is_directory) {
            continue;
        }
        if (pattern.matches(path)) {
            return true;
        }
    }
    return false;
}

} // namespace forge::core

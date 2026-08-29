#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace forge::core {

// A deliberate SUBSET of gitignore syntax — not full compatibility.
// Supported: blank lines, "#" comments, a leading "/" to anchor a
// pattern to the repo root, a trailing "/" to match directories only,
// and "*"/"?" glob wildcards within a single path segment (never
// crossing "/"). NOT supported: "!" negation, "**", "[...]" character
// classes, or escaping — a documented scope limitation, not an
// oversight; a real caller needing those should say so and this can be
// extended then.
class IgnoreRules {
public:
    static IgnoreRules parse(std::string_view forgeignore_content);

    // `path` is repo-relative and POSIX-separated (see
    // core/tree_builder.hpp's convention). Checking directories as they
    // are visited — before descending — is what makes a directory-only
    // pattern also exclude everything under it, without this class
    // needing to track ancestry itself.
    bool is_ignored(std::string_view path, bool is_directory) const;

private:
    struct Pattern {
        std::string text; // pattern with anchoring "/" and trailing "/" already stripped
        bool anchored;
        bool directory_only;

        bool matches(std::string_view path) const;
    };

    explicit IgnoreRules(std::vector<Pattern> patterns);

    std::vector<Pattern> patterns_;
};

} // namespace forge::core

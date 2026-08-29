#include "core/ignore_rules.hpp"
#include "support/test_framework.hpp"

using forge::core::IgnoreRules;

FORGE_TEST_CASE(ignore_rules_ignores_comments_and_blank_lines) {
    const auto rules = IgnoreRules::parse("# comment\n\n*.log\n");
    FORGE_CHECK(rules.is_ignored("app.log", false));
    FORGE_CHECK(!rules.is_ignored("# comment", false));
}

FORGE_TEST_CASE(unanchored_pattern_matches_basename_at_any_depth) {
    const auto rules = IgnoreRules::parse("build\n");
    FORGE_CHECK(rules.is_ignored("build", false));
    FORGE_CHECK(rules.is_ignored("sub/build", false));
    FORGE_CHECK(!rules.is_ignored("buildx", false));
}

FORGE_TEST_CASE(wildcard_matches_within_one_segment_only) {
    const auto rules = IgnoreRules::parse("*.log\n");
    FORGE_CHECK(rules.is_ignored("app.log", false));
    FORGE_CHECK(rules.is_ignored("sub/app.log", false));
    FORGE_CHECK(!rules.is_ignored("applog", false));
}

FORGE_TEST_CASE(directory_only_pattern_requires_is_directory) {
    const auto rules = IgnoreRules::parse("build/\n");
    FORGE_CHECK(rules.is_ignored("build", true));
    FORGE_CHECK(!rules.is_ignored("build", false));
}

FORGE_TEST_CASE(leading_slash_anchors_to_repo_root) {
    const auto rules = IgnoreRules::parse("/only-root.txt\n");
    FORGE_CHECK(rules.is_ignored("only-root.txt", false));
    FORGE_CHECK(!rules.is_ignored("sub/only-root.txt", false));
}

FORGE_TEST_CASE(internal_slash_also_anchors_the_pattern) {
    const auto rules = IgnoreRules::parse("sub/nested.txt\n");
    FORGE_CHECK(rules.is_ignored("sub/nested.txt", false));
    FORGE_CHECK(!rules.is_ignored("other/sub/nested.txt", false));
}

FORGE_TEST_CASE(question_mark_matches_exactly_one_character) {
    const auto rules = IgnoreRules::parse("file?.txt\n");
    FORGE_CHECK(rules.is_ignored("file1.txt", false));
    FORGE_CHECK(!rules.is_ignored("file12.txt", false));
}

FORGE_TEST_CASE(tolerates_crlf_line_endings) {
    const auto rules = IgnoreRules::parse("*.log\r\nbuild/\r\n");
    FORGE_CHECK(rules.is_ignored("app.log", false));
    FORGE_CHECK(rules.is_ignored("build", true));
}

FORGE_TEST_CASE(unmatched_path_is_not_ignored) {
    const auto rules = IgnoreRules::parse("*.log\n");
    FORGE_CHECK(!rules.is_ignored("src/main.cpp", false));
}

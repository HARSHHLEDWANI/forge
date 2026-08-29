#include "storage/safe_path.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::storage::resolve_within_root;
using forge::test::TempDir;

FORGE_TEST_CASE(resolve_within_root_accepts_simple_relative_path) {
    TempDir root;
    const auto result = resolve_within_root(root.path(), "a/b.txt");
    FORGE_CHECK(result.has_value());
    FORGE_CHECK(result->string().find(root.path().string()) == 0);
}

FORGE_TEST_CASE(resolve_within_root_rejects_empty_path) {
    TempDir root;
    FORGE_CHECK(!resolve_within_root(root.path(), "").has_value());
}

FORGE_TEST_CASE(resolve_within_root_rejects_absolute_path) {
    TempDir root;
    const std::filesystem::path outside = root.path().parent_path() / "elsewhere";
    FORGE_CHECK(!resolve_within_root(root.path(), outside.string()).has_value());
}

FORGE_TEST_CASE(resolve_within_root_rejects_parent_traversal) {
    TempDir root;
    FORGE_CHECK(!resolve_within_root(root.path(), "../escape").has_value());
    FORGE_CHECK(!resolve_within_root(root.path(), "a/../../escape").has_value());
}

FORGE_TEST_CASE(resolve_within_root_does_not_false_positive_on_sibling_prefix) {
    TempDir root;
    // A sibling directory whose name is a *string* prefix/superset of the
    // root's name must never be treated as "inside" root.
    const std::filesystem::path sibling = root.path().string() + "-sibling";
    std::filesystem::create_directories(sibling);
    const auto result = resolve_within_root(root.path(), sibling.string());
    FORGE_CHECK(!result.has_value());
    std::filesystem::remove_all(sibling);
}

FORGE_TEST_CASE(resolve_within_root_allows_root_itself) {
    TempDir root;
    const auto result = resolve_within_root(root.path(), ".");
    FORGE_CHECK(result.has_value());
}

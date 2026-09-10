#include "core/error.hpp"
#include "server/repo_registry.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::server::RepoRegistry;
using forge::test::TempDir;

FORGE_TEST_CASE(repo_registry_reports_nonexistent_repo_as_absent) {
    TempDir root;
    RepoRegistry registry(root.path());
    FORGE_CHECK(!registry.exists("nope"));
}

FORGE_TEST_CASE(ensure_exists_creates_a_fresh_repository) {
    TempDir root;
    RepoRegistry registry(root.path());
    registry.ensure_exists("demo");
    FORGE_CHECK(registry.exists("demo"));
    FORGE_CHECK(std::filesystem::is_directory(registry.forge_dir_for("demo")));
}

FORGE_TEST_CASE(ensure_exists_is_a_no_op_on_an_existing_repository) {
    TempDir root;
    RepoRegistry registry(root.path());
    registry.ensure_exists("demo");
    registry.ensure_exists("demo"); // must not throw or reinitialize destructively
    FORGE_CHECK(registry.exists("demo"));
}

FORGE_TEST_CASE(forge_dir_for_rejects_path_traversal_in_repo_name) {
    TempDir root;
    RepoRegistry registry(root.path());
    bool threw = false;
    try {
        registry.forge_dir_for("../escape");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(forge_dir_for_rejects_a_name_with_a_quote_character) {
    TempDir root;
    RepoRegistry registry(root.path());
    bool threw = false;
    try {
        registry.forge_dir_for("bad\"name");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

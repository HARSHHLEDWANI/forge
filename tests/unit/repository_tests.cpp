#include <fstream>
#include <sstream>
#include <string>

#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::storage::discover_repository_root;
using forge::storage::initialize_repository;
using forge::test::TempDir;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

} // namespace

FORGE_TEST_CASE(initialize_repository_creates_forge_dir_and_config) {
    TempDir dir;
    const auto result = initialize_repository(dir.path());
    FORGE_CHECK(!result.reinitialized);
    FORGE_CHECK(std::filesystem::is_directory(result.forge_dir));

    const std::string config = read_file(result.forge_dir / "config");
    FORGE_CHECK(config.find("format_version = 1") != std::string::npos);
    FORGE_CHECK(config.find("storage_root =") != std::string::npos);
}

FORGE_TEST_CASE(initialize_repository_reports_reinitialized_on_second_call) {
    TempDir dir;
    initialize_repository(dir.path());
    const auto second = initialize_repository(dir.path());
    FORGE_CHECK(second.reinitialized);
}

FORGE_TEST_CASE(initialize_repository_preserves_existing_config_on_reinit) {
    TempDir dir;
    const auto first = initialize_repository(dir.path());
    const std::filesystem::path config_path = first.forge_dir / "config";

    std::ofstream(config_path, std::ios::app) << "custom_key = custom_value\n";

    initialize_repository(dir.path());
    FORGE_CHECK(read_file(config_path).find("custom_key = custom_value") != std::string::npos);
}

FORGE_TEST_CASE(initialize_repository_creates_target_dir_if_missing) {
    TempDir dir;
    const std::filesystem::path target = dir.path() / "nested" / "repo";
    const auto result = initialize_repository(target);
    FORGE_CHECK(std::filesystem::is_directory(target));
    FORGE_CHECK(std::filesystem::is_directory(result.forge_dir));
}

FORGE_TEST_CASE(discover_repository_root_finds_repo_in_current_directory) {
    TempDir dir;
    initialize_repository(dir.path());
    const auto found = discover_repository_root(dir.path());
    FORGE_CHECK(found.has_value());
}

FORGE_TEST_CASE(discover_repository_root_walks_up_from_nested_directory) {
    TempDir dir;
    initialize_repository(dir.path());
    const std::filesystem::path nested = dir.path() / "a" / "b" / "c";
    std::filesystem::create_directories(nested);

    const auto found = discover_repository_root(nested);
    FORGE_CHECK(found.has_value());
}

FORGE_TEST_CASE(discover_repository_root_returns_nullopt_when_absent) {
    TempDir dir; // no .forge created
    const auto found = discover_repository_root(dir.path());
    FORGE_CHECK(!found.has_value());
}

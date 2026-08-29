#include <filesystem>
#include <sstream>
#include <string>

#include "cli/cli.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::cli::Command;
using forge::cli::parse_args;
using forge::cli::run;
using forge::test::TempDir;

FORGE_TEST_CASE(parse_args_empty_is_help) {
    FORGE_CHECK(parse_args({}).command == Command::Help);
}

FORGE_TEST_CASE(parse_args_recognizes_help_forms) {
    FORGE_CHECK(parse_args({"--help"}).command == Command::Help);
    FORGE_CHECK(parse_args({"-h"}).command == Command::Help);
    FORGE_CHECK(parse_args({"help"}).command == Command::Help);
}

FORGE_TEST_CASE(parse_args_recognizes_version_forms) {
    FORGE_CHECK(parse_args({"--version"}).command == Command::Version);
    FORGE_CHECK(parse_args({"version"}).command == Command::Version);
}

FORGE_TEST_CASE(parse_args_reports_unknown_command_verbatim) {
    const auto result = parse_args({"frobnicate", "extra-arg"});
    FORGE_CHECK(result.command == Command::Unknown);
    FORGE_CHECK(result.unrecognized == "frobnicate");
}

FORGE_TEST_CASE(run_help_prints_usage_and_succeeds) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"--help"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("Usage: forge") != std::string::npos);
    FORGE_CHECK(err.str().empty());
}

FORGE_TEST_CASE(run_no_args_prints_usage_and_succeeds) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("Usage: forge") != std::string::npos);
}

FORGE_TEST_CASE(run_version_prints_version_and_succeeds) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"version"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("forge ") != std::string::npos);
}

FORGE_TEST_CASE(run_unknown_command_reports_error_and_fails) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"bogus"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("bogus") != std::string::npos);
}

FORGE_TEST_CASE(parse_args_init_defaults_target_to_current_directory) {
    const auto result = parse_args({"init"});
    FORGE_CHECK(result.command == Command::Init);
    FORGE_CHECK(result.init_target == ".");
}

FORGE_TEST_CASE(parse_args_init_accepts_explicit_target) {
    const auto result = parse_args({"init", "some/path"});
    FORGE_CHECK(result.command == Command::Init);
    FORGE_CHECK(result.init_target == "some/path");
}

FORGE_TEST_CASE(run_init_creates_repository_and_succeeds) {
    TempDir dir;
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"init", dir.path().string()}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("Initialized empty Forge repository") != std::string::npos);
    FORGE_CHECK(std::filesystem::is_directory(dir.path() / ".forge"));
}

FORGE_TEST_CASE(run_init_twice_reports_reinitialized) {
    TempDir dir;
    std::ostringstream out1;
    std::ostringstream err1;
    run({"init", dir.path().string()}, out1, err1);

    std::ostringstream out2;
    std::ostringstream err2;
    const int code = run({"init", dir.path().string()}, out2, err2);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out2.str().find("Reinitialized existing Forge repository") != std::string::npos);
}

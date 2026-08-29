#include <sstream>
#include <string>

#include "cli/cli.hpp"
#include "support/test_framework.hpp"

using forge::cli::Command;
using forge::cli::parse_args;
using forge::cli::run;

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

#include <filesystem>
#include <fstream>
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

namespace {

// `add` resolves its pathspec (and discovers the repository) relative to
// the process's current directory, like a real CLI. This guard lets
// tests change it temporarily without leaking state into whichever test
// happens to run next in this shared-process test binary.
struct CwdGuard {
    std::filesystem::path original = std::filesystem::current_path();
    ~CwdGuard() { std::filesystem::current_path(original); }
};

} // namespace

FORGE_TEST_CASE(parse_args_add_requires_explicit_target) {
    const auto result = parse_args({"add"});
    FORGE_CHECK(result.command == Command::Add);
    FORGE_CHECK(result.add_target.empty());
}

FORGE_TEST_CASE(parse_args_add_accepts_target) {
    const auto result = parse_args({"add", "."});
    FORGE_CHECK(result.command == Command::Add);
    FORGE_CHECK(result.add_target == ".");
}

FORGE_TEST_CASE(run_add_with_no_target_is_a_usage_error) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"add"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("nothing added") != std::string::npos);
}

FORGE_TEST_CASE(run_add_outside_a_repository_fails) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"add", "."}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("not a forge repository") != std::string::npos);
}

FORGE_TEST_CASE(run_add_dot_stages_working_tree) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());

    std::ostringstream init_out;
    std::ostringstream init_err;
    run({"init"}, init_out, init_err);
    {
        std::ofstream file("tracked.txt", std::ios::binary);
        file << "content";
    }

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"add", "."}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("add 'tracked.txt'") != std::string::npos);
    FORGE_CHECK(err.str().empty());
}

namespace {

// Discard buffer for setup calls (init/add/commit) whose own output a test
// doesn't care about checking.
std::ostringstream& out_sink() {
    static std::ostringstream sink;
    sink.str("");
    sink.clear();
    return sink;
}

void configure_author(const std::filesystem::path& repo_dir) {
    std::ofstream config(repo_dir / ".forge" / "config", std::ios::app);
    config << "author_name = Test Author\n";
    config << "author_email = test@example.com\n";
}

} // namespace

FORGE_TEST_CASE(run_commit_without_message_is_a_usage_error) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"commit"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("commit message required") != std::string::npos);
}

FORGE_TEST_CASE(run_commit_without_author_configured_fails) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    std::ofstream("tracked.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"commit", "-m", "first"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("no author identity configured") != std::string::npos);
}

FORGE_TEST_CASE(run_commit_creates_commit_and_advances_branch) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("tracked.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"commit", "-m", "first commit"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("first commit") != std::string::npos);
    FORGE_CHECK(err.str().empty());
}

FORGE_TEST_CASE(run_commit_with_nothing_staged_and_no_prior_commit_still_commits_empty_tree) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"commit", "-m", "empty"}, out, err);
    FORGE_CHECK(code == 0);
}

FORGE_TEST_CASE(run_commit_twice_with_no_changes_fails) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("tracked.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"commit", "-m", "second"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("nothing to commit") != std::string::npos);
}

FORGE_TEST_CASE(run_log_with_no_commits_reports_that) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"log"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("no commits yet") != std::string::npos);
}

FORGE_TEST_CASE(run_log_shows_committed_history) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("tracked.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first commit"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"log"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("first commit") != std::string::npos);
    FORGE_CHECK(out.str().find("Test Author <test@example.com>") != std::string::npos);
}

FORGE_TEST_CASE(run_branch_with_no_commits_lists_nothing) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"branch"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().empty());
}

FORGE_TEST_CASE(run_branch_lists_current_branch_after_first_commit) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("tracked.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"branch"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("* main") != std::string::npos);
}

FORGE_TEST_CASE(run_branch_with_name_creates_new_branch) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("tracked.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"branch", "feature"}, out, err);
    FORGE_CHECK(code == 0);

    std::ostringstream list_out;
    std::ostringstream list_err;
    run({"branch"}, list_out, list_err);
    FORGE_CHECK(list_out.str().find("  feature") != std::string::npos);
    FORGE_CHECK(list_out.str().find("* main") != std::string::npos);
}

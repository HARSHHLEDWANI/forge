#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "cli/cli.hpp"
#include "server/app.hpp"
#include "storage/credential_store.hpp"
#include "support/http_test_client.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"
#include "transport/http_server.hpp"

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

FORGE_TEST_CASE(run_no_args_enters_interactive_mode) {
    std::istringstream in("quit\n");
    std::ostringstream out;
    std::ostringstream err;
    const int code = forge::cli::run({}, in, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("interactive mode") != std::string::npos);
    FORGE_CHECK(out.str().find("forge> ") != std::string::npos);
}

FORGE_TEST_CASE(run_completion_bash_prints_a_completion_script) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"completion", "bash"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("_forge_completions") != std::string::npos);
    FORGE_CHECK(out.str().find("complete -F _forge_completions forge") != std::string::npos);
}

FORGE_TEST_CASE(run_completion_rejects_an_unsupported_shell) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"completion", "fish"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("fish") != std::string::npos);
}

FORGE_TEST_CASE(run_unknown_command_suggests_a_close_match) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"stauts"}, out, err); // typo for "status"
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("did you mean 'status'") != std::string::npos);
}

FORGE_TEST_CASE(run_help_with_topic_prints_that_commands_details) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"help", "merge"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("forge merge") != std::string::npos);
    FORGE_CHECK(out.str().find("Merge a branch or commit into the current branch") != std::string::npos);
}

FORGE_TEST_CASE(run_help_with_unknown_topic_fails) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"help", "bogus"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("bogus") != std::string::npos);
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

FORGE_TEST_CASE(run_interactive_status_needs_no_confirmation_and_prints_a_next_hint) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());

    std::istringstream in("status\nquit\n");
    std::ostringstream out;
    std::ostringstream err;
    const int code = forge::cli::run({}, in, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("nothing to commit") != std::string::npos);
    FORGE_CHECK(out.str().find("Next:") != std::string::npos);
    FORGE_CHECK(out.str().find("Run 'status'?") == std::string::npos); // read-only: no confirmation prompt
}

FORGE_TEST_CASE(run_interactive_declining_confirmation_skips_a_destructive_command) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("a.txt", std::ios::binary) << "on main";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());
    run({"branch", "feature"}, out_sink(), out_sink());

    std::istringstream in("switch feature\nn\nquit\n");
    std::ostringstream out;
    std::ostringstream err;
    forge::cli::run({}, in, out, err);
    FORGE_CHECK(out.str().find("Run 'switch feature'? [y/N]") != std::string::npos);
    FORGE_CHECK(out.str().find("Cancelled.") != std::string::npos);

    std::ostringstream branch_out;
    std::ostringstream branch_err;
    run({"branch"}, branch_out, branch_err);
    FORGE_CHECK(branch_out.str().find("* main") != std::string::npos); // switch was cancelled
}

FORGE_TEST_CASE(run_interactive_confirming_runs_a_destructive_command) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("a.txt", std::ios::binary) << "on main";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());
    run({"branch", "feature"}, out_sink(), out_sink());

    std::istringstream in("switch feature\ny\nquit\n");
    std::ostringstream out;
    std::ostringstream err;
    forge::cli::run({}, in, out, err);
    FORGE_CHECK(out.str().find("Switched to branch 'feature'") != std::string::npos);

    std::ostringstream branch_out;
    std::ostringstream branch_err;
    run({"branch"}, branch_out, branch_err);
    FORGE_CHECK(branch_out.str().find("* feature") != std::string::npos);
}

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

FORGE_TEST_CASE(run_switch_without_target_is_a_usage_error) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"switch"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("branch name required") != std::string::npos);
}

FORGE_TEST_CASE(run_switch_to_unknown_branch_fails) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"switch", "nope"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("no such branch") != std::string::npos);
}

FORGE_TEST_CASE(run_switch_moves_head_and_working_tree) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("a.txt", std::ios::binary) << "on main";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "main commit"}, out_sink(), out_sink());
    run({"branch", "feature"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"switch", "feature"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("Switched to branch 'feature'") != std::string::npos);

    std::ostringstream branch_out;
    std::ostringstream branch_err;
    run({"branch"}, branch_out, branch_err);
    FORGE_CHECK(branch_out.str().find("* feature") != std::string::npos);
}

FORGE_TEST_CASE(run_checkout_without_target_is_a_usage_error) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"checkout"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("a branch or commit is required") != std::string::npos);
}

FORGE_TEST_CASE(run_checkout_unknown_revision_fails) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"checkout", "nope"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("unknown revision or branch") != std::string::npos);
}

FORGE_TEST_CASE(run_checkout_detaches_head_at_a_commit) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("a.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());

    std::ostringstream log_out;
    std::ostringstream log_err;
    run({"log"}, log_out, log_err);
    // "commit <64-hex-chars>\n..."
    const std::string log_output = log_out.str();
    const std::string full_hash = log_output.substr(7, 64);

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"checkout", full_hash}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("HEAD is now at") != std::string::npos);

    std::ostringstream branch_out;
    std::ostringstream branch_err;
    run({"branch"}, branch_out, branch_err);
    FORGE_CHECK(branch_out.str().find("* main") == std::string::npos); // detached: no branch marked current
}

FORGE_TEST_CASE(run_status_on_clean_repo_reports_clean) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("a.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"status"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("On branch main") != std::string::npos);
    FORGE_CHECK(out.str().find("nothing to commit, working tree clean") != std::string::npos);
}

FORGE_TEST_CASE(run_status_reports_staged_unstaged_and_untracked) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("a.txt", std::ios::binary) << "v1";
    std::ofstream("b.txt", std::ios::binary) << "v1";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());

    std::ofstream("a.txt", std::ios::binary) << "staged edit";
    run({"add", "a.txt"}, out_sink(), out_sink());
    std::ofstream("b.txt", std::ios::binary) << "unstaged edit";
    std::ofstream("c.txt", std::ios::binary) << "new";

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"status"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("Changes to be committed:") != std::string::npos);
    FORGE_CHECK(out.str().find("modified:   a.txt") != std::string::npos);
    FORGE_CHECK(out.str().find("Changes not staged for commit:") != std::string::npos);
    FORGE_CHECK(out.str().find("modified:   b.txt") != std::string::npos);
    FORGE_CHECK(out.str().find("Untracked files:") != std::string::npos);
    FORGE_CHECK(out.str().find("c.txt") != std::string::npos);
}

FORGE_TEST_CASE(run_diff_shows_unstaged_line_changes) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("a.txt", std::ios::binary) << "line one\nline two\n";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());

    std::ofstream("a.txt", std::ios::binary) << "line one\nline changed\n";

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"diff"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("--- a/a.txt") != std::string::npos);
    FORGE_CHECK(out.str().find("-line two") != std::string::npos);
    FORGE_CHECK(out.str().find("+line changed") != std::string::npos);
}

FORGE_TEST_CASE(run_diff_excludes_untracked_files) {
    TempDir dir;
    CwdGuard guard;
    std::filesystem::current_path(dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(dir.path());
    std::ofstream("a.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());
    std::ofstream("untracked.txt", std::ios::binary) << "new";

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"diff"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().empty());
}

namespace {

// `forge login`/`forge logout` write to $FORGE_HOME/.forge/credentials
// (cli.cpp's open_credential_store honors FORGE_HOME exactly like
// resolve_author honors FORGE_AUTHOR_NAME) — this guard points that at
// a throwaway TempDir for the duration of a test and restores whatever
// was there before, the same isolation CwdGuard gives the working
// directory.
struct ForgeHomeGuard {
    TempDir home;
    std::optional<std::string> original;

    ForgeHomeGuard() {
        if (const char* existing = std::getenv("FORGE_HOME")) {
            original = existing;
        }
#if defined(_WIN32)
        _putenv_s("FORGE_HOME", home.path().string().c_str());
#else
        setenv("FORGE_HOME", home.path().string().c_str(), 1);
#endif
    }

    ~ForgeHomeGuard() {
#if defined(_WIN32)
        _putenv_s("FORGE_HOME", original ? original->c_str() : "");
#else
        if (original) {
            setenv("FORGE_HOME", original->c_str(), 1);
        } else {
            unsetenv("FORGE_HOME");
        }
#endif
    }
};

struct RunningCliServer {
    TempDir repos_root;
    TempDir data_root;
    forge::transport::HttpServer http_server{"127.0.0.1", 0};
    std::thread thread;

    RunningCliServer() {
        forge::server::wire_routes(http_server, repos_root.path(), data_root.path());
        http_server.start();
        thread = std::thread([this] { http_server.serve(); });
    }

    ~RunningCliServer() {
        http_server.stop();
        thread.join();
    }

    std::string url() const { return "forge://127.0.0.1:" + std::to_string(http_server.port()); }

    void register_user(const std::string& username, const std::string& password) const {
        std::ostringstream request;
        request << "POST /users?username=" << username << " HTTP/1.1\r\nHost: x\r\nContent-Length: " << password.size()
                << "\r\n\r\n"
                << password;
        forge::test::send_raw_http_request(http_server.port(), request.str());
    }
};

} // namespace

FORGE_TEST_CASE(parse_args_login_reads_url_username_and_password) {
    const auto result =
        parse_args({"login", "forge://example.com:8080", "--username", "alice", "--password", "hunter2"});
    FORGE_CHECK(result.command == Command::Login);
    FORGE_CHECK(result.login_url == "forge://example.com:8080");
    FORGE_CHECK(result.login_username == "alice");
    FORGE_CHECK(result.login_password == "hunter2");
}

FORGE_TEST_CASE(parse_args_logout_reads_url) {
    const auto result = parse_args({"logout", "forge://example.com:8080"});
    FORGE_CHECK(result.command == Command::Logout);
    FORGE_CHECK(result.logout_url == "forge://example.com:8080");
}

FORGE_TEST_CASE(run_login_without_username_is_a_usage_error) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"login", "forge://example.com:8080"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("--username") != std::string::npos);
}

FORGE_TEST_CASE(run_login_rejects_a_malformed_server_address) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"login", "not-a-url", "--username", "alice", "--password", "x"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("invalid server address") != std::string::npos);
}

FORGE_TEST_CASE(run_login_with_wrong_password_fails) {
    ForgeHomeGuard home_guard;
    RunningCliServer server;
    server.register_user("alice", "hunter2");

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"login", server.url(), "--username", "alice", "--password", "wrong"}, out, err);
    FORGE_CHECK(code != 0);
    FORGE_CHECK(err.str().find("login failed") != std::string::npos);
}

FORGE_TEST_CASE(run_login_with_the_right_password_saves_a_credential) {
    ForgeHomeGuard home_guard;
    RunningCliServer server;
    server.register_user("alice", "hunter2");

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"login", server.url(), "--username", "alice", "--password", "hunter2"}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("Logged in to") != std::string::npos);

    const forge::storage::CredentialStore store(home_guard.home.path());
    const std::optional<std::string> saved = store.find("forge://127.0.0.1:" + std::to_string(server.http_server.port()));
    FORGE_CHECK(saved.has_value());
    FORGE_CHECK(!saved->empty());
}

FORGE_TEST_CASE(run_logout_removes_a_saved_credential) {
    ForgeHomeGuard home_guard;
    RunningCliServer server;
    server.register_user("alice", "hunter2");
    run({"login", server.url(), "--username", "alice", "--password", "hunter2"}, out_sink(), out_sink());

    std::ostringstream out;
    std::ostringstream err;
    const int code = run({"logout", server.url()}, out, err);
    FORGE_CHECK(code == 0);
    FORGE_CHECK(out.str().find("Logged out of") != std::string::npos);

    const forge::storage::CredentialStore store(home_guard.home.path());
    FORGE_CHECK(!store.find(server.url()).has_value());
}

FORGE_TEST_CASE(run_push_uses_a_saved_credential_to_satisfy_a_write_permission_check) {
    ForgeHomeGuard home_guard;
    RunningCliServer server;
    server.register_user("alice", "hunter2");
    run({"login", server.url(), "--username", "alice", "--password", "hunter2"}, out_sink(), out_sink());

    TempDir repo_dir;
    CwdGuard guard;
    std::filesystem::current_path(repo_dir.path());
    run({"init"}, out_sink(), out_sink());
    configure_author(repo_dir.path());
    std::ofstream("a.txt", std::ios::binary) << "content";
    run({"add", "."}, out_sink(), out_sink());
    run({"commit", "-m", "first"}, out_sink(), out_sink());

    const std::string remote_url = server.url() + "/demo";

    // First push, authenticated as alice via the saved token: creates
    // the repo ("push to create") and bootstraps alice as its Admin
    // (server/app.cpp's POST /ref bootstrap) — the same path an
    // anonymous push would take, just now with a real identity behind it.
    std::ostringstream first_out;
    std::ostringstream first_err;
    FORGE_CHECK(run({"push", remote_url}, first_out, first_err) == 0);

    // Logged out: the repo now has a recorded permission (alice/Admin),
    // so a second push with no saved credential must be rejected.
    run({"logout", server.url()}, out_sink(), out_sink());
    std::ostringstream second_out;
    std::ostringstream second_err;
    const int second_code = run({"push", remote_url}, second_out, second_err);
    FORGE_CHECK(second_code != 0);
    FORGE_CHECK(second_err.str().find("write access is required") != std::string::npos);

    // Logging back in restores push access via the saved token again.
    run({"login", server.url(), "--username", "alice", "--password", "hunter2"}, out_sink(), out_sink());
    std::ostringstream third_out;
    std::ostringstream third_err;
    FORGE_CHECK(run({"push", remote_url}, third_out, third_err) == 0);
}

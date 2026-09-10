#pragma once

#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace forge::cli {

enum class Command {
    Help, Version, Init, Add, Commit, Log, Branch, Switch, Checkout, Status, Diff, Merge, Completion, Verify,
    Clone, Fetch, Push, Backup, Restore, Unknown
};

struct ParseResult {
    Command command = Command::Help;
    // Populated only when command == Unknown, so callers can report
    // exactly what the user typed.
    std::string unrecognized;
    // Populated only when command == Help and a topic was given (`forge
    // help <command>`); empty means the full command list.
    std::string help_topic;
    // Populated only when command == Init; defaults to the current
    // directory, matching `forge init [path]`.
    std::string init_target = ".";
    // Populated only when command == Add; empty means "no pathspec was
    // given", a usage error `run()` reports rather than parse_args.
    std::string add_target;
    // Populated only when command == Commit; empty means "no -m <message>
    // was given", a usage error `run()` reports rather than parse_args.
    std::string commit_message;
    // Populated only when command == Branch; empty means "list branches"
    // rather than "create one", matching `git branch` with no argument.
    std::string branch_name;
    // Populated only when command == Switch; empty is a usage error.
    std::string switch_target;
    // Populated only when command == Checkout; empty is a usage error.
    std::string checkout_target;
    // Populated only when command == Merge; empty is a usage error.
    std::string merge_target;
    // Populated only when command == Merge; empty means `run()` should
    // generate a default merge commit message.
    std::string merge_message;
    // Populated only when command == Completion; the shell to generate a
    // completion script for (currently only "bash" is supported).
    std::string completion_shell;
    // Populated only when command == Clone; empty is a usage error.
    std::string clone_url;
    // Populated only when command == Clone; empty means "derive it from
    // the remote URL's repo name", matching `git clone <url>`.
    std::string clone_target;
    // Populated only when command == Fetch; empty is a usage error.
    std::string fetch_url;
    // Populated only when command == Push; empty is a usage error.
    std::string push_url;
    // Populated only when command == Push; empty means "push HEAD's
    // current branch".
    std::string push_branch;
    // Populated only when command == Push; true if "--force" was given.
    bool push_force = false;
    // Populated only when command == Backup; empty is a usage error.
    std::string backup_destination;
    // Populated only when command == Restore; both empty is a usage error.
    std::string restore_source;
    std::string restore_target;
};

// Pure and side-effect free so it is easy to unit test independently of
// process argv and stream plumbing.
ParseResult parse_args(const std::vector<std::string>& args);

// One entry in the command registry (see cli.cpp): the single source of
// truth for a subcommand's name, argument hint, and one-line summary.
// Drives the generated help text, `forge completion bash`, unknown-command
// "did you mean" suggestions, and the interactive menu, so those four
// surfaces can never drift out of sync with each other.
struct CommandSpec {
    std::string_view name;
    std::string_view usage_args; // "" if the command takes no arguments
    std::string_view summary;
    // Whether interactive mode (see run_interactive below) asks "Run
    // this? [y/N]" before executing it. Read-only/additive commands
    // (status, add, commit, ...) don't; commands that can overwrite
    // working-tree files (switch, checkout, merge) do, as a courtesy on
    // top of the safety checks those commands already enforce
    // themselves (see checkout.hpp/merge.hpp) — see cli.md's "confirmations
    // for destructive actions" rule.
    bool confirm_in_interactive_mode;
};

// All direct subcommands (excluding the meta-commands --help/version/
// completion, which aren't meaningful to complete, confirm, or suggest
// as typo corrections the same way). Sorted to match kUsage's order.
const std::vector<CommandSpec>& command_registry();

// Executes the parsed command, writing to `out`/`err`. Returns the
// process exit code: 0 on success, non-zero on usage error.
int execute_command(const ParseResult& result, std::ostream& out, std::ostream& err);

// `forge` with no arguments: a REPL reading command lines from `in` (one
// `parse_args`-style command per line) until "quit"/"exit" or end of
// input, echoing a prompt and a short "try next" hint after each command
// so users can discover status/staging/commits/branches/history/diff/
// merge without memorizing the direct-invocation syntax first (see
// cli.md). Commands flagged confirm_in_interactive_mode in the registry
// ask for a "y"/"n" confirmation line, read from `in`, before running.
int run_interactive(std::istream& in, std::ostream& out, std::ostream& err);

// Top-level entry point: `args.empty()` launches interactive mode
// (reading from `in`); otherwise parses and executes a single command.
int run(const std::vector<std::string>& args, std::istream& in, std::ostream& out, std::ostream& err);

// Convenience overload for direct (non-interactive) invocations, which
// never read `in`: process argv, tests, and any caller that already
// knows `args` isn't empty. Bare `forge` (empty args) still works here
// too, reading interactive input from std::cin.
int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

} // namespace forge::cli

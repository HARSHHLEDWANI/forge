#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace forge::cli {

enum class Command { Help, Version, Init, Add, Commit, Log, Branch, Unknown };

struct ParseResult {
    Command command = Command::Help;
    // Populated only when command == Unknown, so callers can report
    // exactly what the user typed.
    std::string unrecognized;
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
};

// Pure and side-effect free so it is easy to unit test independently of
// process argv and stream plumbing.
ParseResult parse_args(const std::vector<std::string>& args);

// Executes the parsed command, writing to the given streams. Returns the
// process exit code: 0 on success, non-zero on usage error.
int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

} // namespace forge::cli

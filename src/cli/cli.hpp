#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace forge::cli {

enum class Command { Help, Version, Unknown };

struct ParseResult {
    Command command;
    // Populated only when command == Unknown, so callers can report
    // exactly what the user typed.
    std::string unrecognized;
};

// Pure and side-effect free so it is easy to unit test independently of
// process argv and stream plumbing.
ParseResult parse_args(const std::vector<std::string>& args);

// Executes the parsed command, writing to the given streams. Returns the
// process exit code: 0 on success, non-zero on usage error.
int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

} // namespace forge::cli

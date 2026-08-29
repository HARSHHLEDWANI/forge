#include "cli/cli.hpp"

#include "core/version.hpp"

namespace forge::cli {

namespace {

constexpr std::string_view kUsage =
    "Usage: forge <command> [options]\n"
    "\n"
    "Commands:\n"
    "  version      Print the Forge version\n"
    "  --help, -h   Show this help message\n";

} // namespace

ParseResult parse_args(const std::vector<std::string>& args) {
    if (args.empty()) {
        return {Command::Help, {}};
    }

    const std::string& first = args.front();
    if (first == "--help" || first == "-h" || first == "help") {
        return {Command::Help, {}};
    }
    if (first == "--version" || first == "version") {
        return {Command::Version, {}};
    }
    return {Command::Unknown, first};
}

int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const ParseResult result = parse_args(args);
    switch (result.command) {
        case Command::Help:
            out << kUsage;
            return 0;
        case Command::Version:
            out << "forge " << core::kVersion << '\n';
            return 0;
        case Command::Unknown:
            err << "forge: '" << result.unrecognized << "' is not a forge command\n";
            err << kUsage;
            return 1;
    }
    return 1;
}

} // namespace forge::cli

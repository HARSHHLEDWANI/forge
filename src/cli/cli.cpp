#include "cli/cli.hpp"

#include <filesystem>

#include "core/error.hpp"
#include "core/version.hpp"
#include "storage/repository.hpp"

namespace forge::cli {

namespace {

constexpr std::string_view kUsage =
    "Usage: forge <command> [options]\n"
    "\n"
    "Commands:\n"
    "  init [path]  Create a new Forge repository\n"
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
    if (first == "init") {
        ParseResult result{Command::Init, {}};
        if (args.size() >= 2) {
            result.init_target = args[1];
        }
        return result;
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
        case Command::Init: {
            try {
                const storage::InitResult init_result =
                    storage::initialize_repository(result.init_target);
                const std::filesystem::path absolute_dir =
                    std::filesystem::absolute(init_result.forge_dir).lexically_normal();
                out << (init_result.reinitialized ? "Reinitialized existing Forge repository in "
                                                   : "Initialized empty Forge repository in ")
                    << absolute_dir.string() << '\n';
                return 0;
            } catch (const core::ForgeError& e) {
                err << "forge: " << e.what() << '\n';
                return 1;
            }
        }
        case Command::Unknown:
            err << "forge: '" << result.unrecognized << "' is not a forge command\n";
            err << kUsage;
            return 1;
    }
    return 1;
}

} // namespace forge::cli

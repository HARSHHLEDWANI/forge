#include "cli/cli.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/error.hpp"
#include "core/ignore_rules.hpp"
#include "core/staging.hpp"
#include "core/version.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/repository.hpp"

namespace forge::cli {

namespace {

constexpr std::string_view kUsage =
    "Usage: forge <command> [options]\n"
    "\n"
    "Commands:\n"
    "  init [path]  Create a new Forge repository\n"
    "  add <path>   Stage a file or directory\n"
    "  version      Print the Forge version\n"
    "  --help, -h   Show this help message\n";

core::IgnoreRules load_ignore_rules(const std::filesystem::path& repo_root) {
    const std::filesystem::path ignore_path = repo_root / storage::kIgnoreFileName;
    if (!std::filesystem::exists(ignore_path)) {
        return core::IgnoreRules::parse("");
    }
    std::ifstream in(ignore_path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return core::IgnoreRules::parse(buffer.str());
}

} // namespace

ParseResult parse_args(const std::vector<std::string>& args) {
    // Default-constructed (command == Help) and then assigned, rather
    // than partial brace-init, so this doesn't grow a
    // -Wmissing-field-initializers warning every time ParseResult gains a
    // field only some commands use.
    ParseResult result;
    if (args.empty()) {
        return result;
    }

    const std::string& first = args.front();
    if (first == "--help" || first == "-h" || first == "help") {
        return result;
    }
    if (first == "--version" || first == "version") {
        result.command = Command::Version;
        return result;
    }
    if (first == "init") {
        result.command = Command::Init;
        if (args.size() >= 2) {
            result.init_target = args[1];
        }
        return result;
    }
    if (first == "add") {
        result.command = Command::Add;
        if (args.size() >= 2) {
            result.add_target = args[1];
        }
        return result;
    }
    result.command = Command::Unknown;
    result.unrecognized = first;
    return result;
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
        case Command::Add: {
            if (result.add_target.empty()) {
                err << "forge: nothing specified, nothing added\n";
                return 1;
            }
            try {
                const std::optional<std::filesystem::path> repo_root = storage::discover_repository_root();
                if (!repo_root) {
                    err << "forge: not a forge repository (or any parent up to the filesystem root)\n";
                    return 1;
                }
                const std::filesystem::path forge_dir = *repo_root / storage::kForgeDirName;
                const storage::RepositoryConfig config = storage::load_config(forge_dir);

                storage::ObjectStore objects(config.storage_root);
                storage::IndexStore index_store(forge_dir / storage::kIndexFileName);
                const core::IgnoreRules ignore_rules = load_ignore_rules(*repo_root);

                const core::AddResult add_result =
                    core::stage_path(objects, index_store, ignore_rules, *repo_root, result.add_target);
                for (const std::string& path : add_result.staged) {
                    out << "add '" << path << "'\n";
                }
                for (const std::string& path : add_result.removed) {
                    out << "remove '" << path << "'\n";
                }
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

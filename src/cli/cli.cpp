#include "cli/cli.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/branching.hpp"
#include "core/commit.hpp"
#include "core/committing.hpp"
#include "core/error.hpp"
#include "core/ignore_rules.hpp"
#include "core/log.hpp"
#include "core/staging.hpp"
#include "core/version.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"

namespace forge::cli {

namespace {

constexpr std::string_view kUsage =
    "Usage: forge <command> [options]\n"
    "\n"
    "Commands:\n"
    "  init [path]     Create a new Forge repository\n"
    "  add <path>      Stage a file or directory\n"
    "  commit -m <msg> Record staged changes as a new commit\n"
    "  log             Show commit history from HEAD\n"
    "  branch [name]   List branches, or create one at HEAD\n"
    "  version         Print the Forge version\n"
    "  --help, -h      Show this help message\n";

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

struct RepoContext {
    std::filesystem::path repo_root;
    std::filesystem::path forge_dir;
};

// Shared by every command that needs an existing repository. Writes the
// standard "not a forge repository" message itself so each call site
// doesn't repeat it.
std::optional<RepoContext> discover_repo_context(std::ostream& err) {
    const std::optional<std::filesystem::path> repo_root = storage::discover_repository_root();
    if (!repo_root) {
        err << "forge: not a forge repository (or any parent up to the filesystem root)\n";
        return std::nullopt;
    }
    return RepoContext{*repo_root, *repo_root / storage::kForgeDirName};
}

// Resolves a commit author from FORGE_AUTHOR_NAME/FORGE_AUTHOR_EMAIL (if
// set) or else the repository config's author_name/author_email. There's
// no user/account system yet (frozen-scope.md defers that to Phase 14),
// so this is the entire identity story for V1 — deliberately no silent
// fallback to an OS username, since a wrong recorded identity is worse
// than an explicit setup error.
std::string resolve_author(const storage::RepositoryConfig& config) {
    const char* env_name = std::getenv("FORGE_AUTHOR_NAME");
    const char* env_email = std::getenv("FORGE_AUTHOR_EMAIL");
    const std::string name = env_name ? env_name : config.author_name;
    const std::string email = env_email ? env_email : config.author_email;
    if (name.empty() || email.empty()) {
        throw core::ForgeError(
            "no author identity configured; set author_name/author_email in .forge/config "
            "or the FORGE_AUTHOR_NAME/FORGE_AUTHOR_EMAIL environment variables");
    }
    return name + " <" + email + ">";
}

std::int64_t current_unix_timestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
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
    if (first == "commit") {
        result.command = Command::Commit;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-m" && i + 1 < args.size()) {
                result.commit_message = args[i + 1];
                ++i;
            }
        }
        return result;
    }
    if (first == "log") {
        result.command = Command::Log;
        return result;
    }
    if (first == "branch") {
        result.command = Command::Branch;
        if (args.size() >= 2) {
            result.branch_name = args[1];
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
                const std::optional<RepoContext> ctx = discover_repo_context(err);
                if (!ctx) {
                    return 1;
                }
                const storage::RepositoryConfig config = storage::load_config(ctx->forge_dir);

                storage::ObjectStore objects(config.storage_root);
                storage::IndexStore index_store(ctx->forge_dir / storage::kIndexFileName);
                const core::IgnoreRules ignore_rules = load_ignore_rules(ctx->repo_root);

                const core::AddResult add_result =
                    core::stage_path(objects, index_store, ignore_rules, ctx->repo_root, result.add_target);
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
        case Command::Commit: {
            if (result.commit_message.empty()) {
                err << "forge: commit message required (-m <message>)\n";
                return 1;
            }
            try {
                const std::optional<RepoContext> ctx = discover_repo_context(err);
                if (!ctx) {
                    return 1;
                }
                const storage::RepositoryConfig config = storage::load_config(ctx->forge_dir);

                storage::ObjectStore objects(config.storage_root);
                storage::IndexStore index_store(ctx->forge_dir / storage::kIndexFileName);
                storage::RefStore refs(ctx->forge_dir);

                const std::string author = resolve_author(config);
                const core::CommitResult commit_result = core::create_commit(
                    objects, index_store, refs, author, result.commit_message, current_unix_timestamp());

                out << "[" << commit_result.commit_id.to_hex().substr(0, 12) << "] " << result.commit_message
                    << '\n';
                return 0;
            } catch (const core::ForgeError& e) {
                err << "forge: " << e.what() << '\n';
                return 1;
            }
        }
        case Command::Log: {
            try {
                const std::optional<RepoContext> ctx = discover_repo_context(err);
                if (!ctx) {
                    return 1;
                }
                const storage::RepositoryConfig config = storage::load_config(ctx->forge_dir);

                storage::ObjectStore objects(config.storage_root);
                storage::RefStore refs(ctx->forge_dir);

                const std::optional<core::ObjectId> head_commit = refs.resolve_head();
                if (!head_commit) {
                    out << "no commits yet\n";
                    return 0;
                }

                for (const core::LogEntry& entry : core::commit_log(objects, *head_commit)) {
                    out << "commit " << entry.id.to_hex() << '\n';
                    out << "Author: " << entry.commit.author << '\n';
                    out << "Date:   " << entry.commit.timestamp << '\n';
                    out << '\n';
                    out << "    " << entry.commit.message << '\n';
                    out << '\n';
                }
                return 0;
            } catch (const core::ForgeError& e) {
                err << "forge: " << e.what() << '\n';
                return 1;
            }
        }
        case Command::Branch: {
            try {
                const std::optional<RepoContext> ctx = discover_repo_context(err);
                if (!ctx) {
                    return 1;
                }
                storage::RefStore refs(ctx->forge_dir);

                if (result.branch_name.empty()) {
                    for (const core::BranchInfo& branch : core::list_branches(refs)) {
                        out << (branch.is_current ? "* " : "  ") << branch.name << '\n';
                    }
                    return 0;
                }

                core::create_branch(refs, result.branch_name);
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

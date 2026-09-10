#include "cli/cli.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/branching.hpp"
#include "core/checkout.hpp"
#include "core/commit.hpp"
#include "core/committing.hpp"
#include "core/diff.hpp"
#include "core/error.hpp"
#include "core/ignore_rules.hpp"
#include "core/log.hpp"
#include "core/merge.hpp"
#include "core/staging.hpp"
#include "core/tree_builder.hpp"
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
    "  switch <name>   Switch to an existing branch\n"
    "  checkout <ref>  Switch to a branch, or detach HEAD at a commit\n"
    "  status          Show staged, unstaged, and untracked changes\n"
    "  diff            Show unstaged changes, line by line\n"
    "  merge <ref>     Merge a branch or commit into the current branch\n"
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

std::string_view status_word(core::ChangeType type) {
    switch (type) {
        case core::ChangeType::Added: return "new file";
        case core::ChangeType::Modified: return "modified";
        case core::ChangeType::Deleted: return "deleted";
    }
    return "changed";
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
    if (first == "switch") {
        result.command = Command::Switch;
        if (args.size() >= 2) {
            result.switch_target = args[1];
        }
        return result;
    }
    if (first == "checkout") {
        result.command = Command::Checkout;
        if (args.size() >= 2) {
            result.checkout_target = args[1];
        }
        return result;
    }
    if (first == "status") {
        result.command = Command::Status;
        return result;
    }
    if (first == "diff") {
        result.command = Command::Diff;
        return result;
    }
    if (first == "merge") {
        result.command = Command::Merge;
        for (std::size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "-m" && i + 1 < args.size()) {
                result.merge_message = args[i + 1];
                ++i;
            } else if (result.merge_target.empty()) {
                result.merge_target = args[i];
            }
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
        case Command::Switch: {
            if (result.switch_target.empty()) {
                err << "forge: branch name required\n";
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

                const std::optional<core::CheckoutTarget> target =
                    core::resolve_checkout_target(refs, objects, result.switch_target);
                if (!target || target->kind != core::CheckoutTargetKind::Branch) {
                    err << "forge: no such branch: " << result.switch_target << '\n';
                    return 1;
                }

                core::checkout(objects, index_store, refs, ctx->repo_root, *target);
                out << "Switched to branch '" << result.switch_target << "'\n";
                return 0;
            } catch (const core::ForgeError& e) {
                err << "forge: " << e.what() << '\n';
                return 1;
            }
        }
        case Command::Checkout: {
            if (result.checkout_target.empty()) {
                err << "forge: a branch or commit is required\n";
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

                const std::optional<core::CheckoutTarget> target =
                    core::resolve_checkout_target(refs, objects, result.checkout_target);
                if (!target) {
                    err << "forge: unknown revision or branch: " << result.checkout_target << '\n';
                    return 1;
                }

                core::checkout(objects, index_store, refs, ctx->repo_root, *target);
                if (target->kind == core::CheckoutTargetKind::Branch) {
                    out << "Switched to branch '" << result.checkout_target << "'\n";
                } else {
                    out << "HEAD is now at " << target->commit_id.to_hex().substr(0, 12) << '\n';
                }
                return 0;
            } catch (const core::ForgeError& e) {
                err << "forge: " << e.what() << '\n';
                return 1;
            }
        }
        case Command::Status: {
            try {
                const std::optional<RepoContext> ctx = discover_repo_context(err);
                if (!ctx) {
                    return 1;
                }
                const storage::RepositoryConfig config = storage::load_config(ctx->forge_dir);

                storage::ObjectStore objects(config.storage_root);
                storage::IndexStore index_store(ctx->forge_dir / storage::kIndexFileName);
                storage::RefStore refs(ctx->forge_dir);
                const core::IgnoreRules ignore_rules = load_ignore_rules(ctx->repo_root);

                const storage::RefStore::Head head = refs.read_head();
                if (head.branch) {
                    out << "On branch " << *head.branch << '\n';
                } else {
                    out << "HEAD detached at " << head.detached_commit->to_hex().substr(0, 12) << '\n';
                }

                const std::optional<core::ObjectId> head_commit = refs.resolve_head();
                const core::Index head_tree_index = head_commit
                                                         ? core::flatten_tree_to_index(
                                                               objects, objects.get_commit(*head_commit).tree_id)
                                                         : core::Index{};
                const core::Index current_index = index_store.load();
                const core::Index working_snapshot = core::snapshot_working_tree(ignore_rules, ctx->repo_root);

                const std::vector<core::EntryChange> staged = core::diff_index(head_tree_index, current_index);
                std::vector<core::EntryChange> unstaged;
                std::vector<core::EntryChange> untracked;
                for (const core::EntryChange& change : core::diff_index(current_index, working_snapshot)) {
                    if (change.type == core::ChangeType::Added) {
                        untracked.push_back(change);
                    } else {
                        unstaged.push_back(change);
                    }
                }

                if (!staged.empty()) {
                    out << "\nChanges to be committed:\n";
                    for (const core::EntryChange& change : staged) {
                        out << "  " << status_word(change.type) << ":   " << change.path << '\n';
                    }
                }
                if (!unstaged.empty()) {
                    out << "\nChanges not staged for commit:\n";
                    for (const core::EntryChange& change : unstaged) {
                        out << "  " << status_word(change.type) << ":   " << change.path << '\n';
                    }
                }
                if (!untracked.empty()) {
                    out << "\nUntracked files:\n";
                    for (const core::EntryChange& change : untracked) {
                        out << "  " << change.path << '\n';
                    }
                }
                if (staged.empty() && unstaged.empty() && untracked.empty()) {
                    out << "\nnothing to commit, working tree clean\n";
                }
                return 0;
            } catch (const core::ForgeError& e) {
                err << "forge: " << e.what() << '\n';
                return 1;
            }
        }
        case Command::Merge: {
            if (result.merge_target.empty()) {
                err << "forge: a branch or commit is required\n";
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
                const storage::RefStore::Head head = refs.read_head();
                std::string message = result.merge_message;
                if (message.empty()) {
                    message = "Merge branch '" + result.merge_target + "'";
                    if (head.branch) {
                        message += " into " + *head.branch;
                    }
                }

                const core::MergeResult merge_result = core::merge(
                    objects, index_store, refs, ctx->repo_root, result.merge_target, author, message,
                    current_unix_timestamp());

                switch (merge_result.outcome) {
                    case core::MergeOutcome::AlreadyUpToDate:
                        out << "Already up to date.\n";
                        return 0;
                    case core::MergeOutcome::FastForward:
                        out << "Fast-forward to " << merge_result.commit_id->to_hex().substr(0, 12) << '\n';
                        return 0;
                    case core::MergeOutcome::Merged:
                        out << "Merge made: [" << merge_result.commit_id->to_hex().substr(0, 12) << "] " << message
                            << '\n';
                        return 0;
                    case core::MergeOutcome::Conflict:
                        out << "Automatic merge failed; fix conflicts and then commit the result:\n";
                        for (const core::MergeConflict& conflict : merge_result.conflicts) {
                            out << "  conflict: " << conflict.path << '\n';
                        }
                        return 1;
                }
                return 1;
            } catch (const core::ForgeError& e) {
                err << "forge: " << e.what() << '\n';
                return 1;
            }
        }
        case Command::Diff: {
            try {
                const std::optional<RepoContext> ctx = discover_repo_context(err);
                if (!ctx) {
                    return 1;
                }
                const storage::RepositoryConfig config = storage::load_config(ctx->forge_dir);

                storage::ObjectStore objects(config.storage_root);
                storage::IndexStore index_store(ctx->forge_dir / storage::kIndexFileName);
                const core::IgnoreRules ignore_rules = load_ignore_rules(ctx->repo_root);

                const core::Index current_index = index_store.load();
                for (const core::FileDiff& file_diff :
                     core::diff_working_tree(objects, current_index, ignore_rules, ctx->repo_root)) {
                    out << core::render_unified_diff(file_diff.path, file_diff.content);
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

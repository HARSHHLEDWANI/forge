#include "cli/cli.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

// The single source of truth for every subcommand's name/args/summary,
// consumed by build_usage(), find_spec(), suggest_command(),
// bash_completion_script(), and run_interactive(). See cli.hpp's
// CommandSpec doc comment for why this exists as one table rather than
// four things that each need to be kept in sync by hand.
const std::vector<CommandSpec>& registry_table() {
    static const std::vector<CommandSpec> table = {
        {"init", "[path]", "Create a new Forge repository", false},
        {"add", "<path>", "Stage a file or directory", false},
        {"commit", "-m <msg>", "Record staged changes as a new commit", false},
        {"log", "", "Show commit history from HEAD", false},
        {"branch", "[name]", "List branches, or create one at HEAD", false},
        {"switch", "<name>", "Switch to an existing branch", true},
        {"checkout", "<ref>", "Switch to a branch, or detach HEAD at a commit", true},
        {"status", "", "Show staged, unstaged, and untracked changes", false},
        {"diff", "", "Show unstaged changes, line by line", false},
        {"merge", "<ref> [-m <msg>]", "Merge a branch or commit into the current branch", true},
        {"completion", "<shell>", "Print a shell completion script", false},
        {"help", "[command]", "Show this help message, or one command's details", false},
        {"version", "", "Print the Forge version", false},
    };
    return table;
}

const CommandSpec* find_spec(std::string_view name) {
    for (const CommandSpec& spec : registry_table()) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
}

std::string build_usage() {
    std::size_t width = std::string_view("--help, -h").size();
    for (const CommandSpec& spec : registry_table()) {
        std::size_t left_width = spec.name.size();
        if (!spec.usage_args.empty()) {
            left_width += 1 + spec.usage_args.size();
        }
        width = std::max(width, left_width);
    }
    width += 2; // gap before the summary column

    std::ostringstream out;
    out << "Usage: forge <command> [options]\n\nCommands:\n";
    for (const CommandSpec& spec : registry_table()) {
        std::string left(spec.name);
        if (!spec.usage_args.empty()) {
            left += ' ';
            left += spec.usage_args;
        }
        out << "  " << left << std::string(width - left.size(), ' ') << spec.summary << '\n';
    }
    const std::string_view help_alias = "--help, -h";
    out << "  " << help_alias << std::string(width - help_alias.size(), ' ') << "Same as 'help'\n";
    out << "\nRun 'forge' with no arguments for interactive mode.\n";
    return out.str();
}

// Standard DP edit-distance table (same style as diff.cpp's lcs_diff),
// used only to catch an honest typo of a real command name — not a
// general-purpose spell checker, so a small fixed cutoff below is enough.
std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::vector<std::size_t>> dp(a.size() + 1, std::vector<std::size_t>(b.size() + 1));
    for (std::size_t i = 0; i <= a.size(); ++i) {
        dp[i][0] = i;
    }
    for (std::size_t j = 0; j <= b.size(); ++j) {
        dp[0][j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        for (std::size_t j = 1; j <= b.size(); ++j) {
            dp[i][j] = a[i - 1] == b[j - 1] ? dp[i - 1][j - 1]
                                             : 1 + std::min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
        }
    }
    return dp[a.size()][b.size()];
}

std::optional<std::string_view> suggest_command(std::string_view typed) {
    constexpr std::size_t kMaxSuggestDistance = 2;
    std::optional<std::string_view> best;
    std::size_t best_distance = 0;
    for (const CommandSpec& spec : registry_table()) {
        const std::size_t distance = edit_distance(typed, spec.name);
        if (distance <= kMaxSuggestDistance && (!best || distance < best_distance)) {
            best = spec.name;
            best_distance = distance;
        }
    }
    return best;
}

std::string bash_completion_script() {
    std::ostringstream out;
    out << "_forge_completions() {\n";
    out << "    local cur\n";
    out << "    cur=\"${COMP_WORDS[COMP_CWORD]}\"\n";
    out << "    if [ \"$COMP_CWORD\" -eq 1 ]; then\n";
    out << "        COMPREPLY=( $(compgen -W \"";
    bool first = true;
    for (const CommandSpec& spec : registry_table()) {
        if (!first) {
            out << ' ';
        }
        out << spec.name;
        first = false;
    }
    out << "\" -- \"$cur\") )\n";
    out << "    fi\n";
    out << "}\n";
    out << "complete -F _forge_completions forge\n";
    return out.str();
}

std::vector<std::string> tokenize_line(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    bool has_current = false;
    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
            has_current = true;
            continue;
        }
        if (!in_quotes && std::isspace(static_cast<unsigned char>(c))) {
            if (has_current) {
                tokens.push_back(current);
                current.clear();
                has_current = false;
            }
            continue;
        }
        current.push_back(c);
        has_current = true;
    }
    if (has_current) {
        tokens.push_back(current);
    }
    return tokens;
}

// A short nudge toward what a user would plausibly do right after this
// command, so interactive mode helps with discovery instead of just
// executing and going silent (cli.md's "useful next-command suggestions").
std::string_view next_hint(Command command) {
    switch (command) {
        case Command::Status: return "'add <path>' to stage changes, or 'diff' to see them line by line.";
        case Command::Add: return "'commit -m <message>' to record what's staged, or 'status' to review it.";
        case Command::Commit: return "'log' for history, or 'branch <name>' to start a new line of work.";
        case Command::Branch: return "'switch <name>' to move onto a branch, or 'merge <name>' to bring one in.";
        case Command::Switch:
        case Command::Checkout: return "'status' to see where you landed, or 'log' for history.";
        case Command::Diff: return "'add <path>' to stage the changes shown above.";
        case Command::Merge: return "'status' to review the result, or 'log'.";
        default: return "";
    }
}

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

const std::vector<CommandSpec>& command_registry() { return registry_table(); }

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
    if (first == "--help" || first == "-h") {
        return result;
    }
    if (first == "help") {
        if (args.size() >= 2) {
            result.help_topic = args[1];
        }
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
    if (first == "completion") {
        result.command = Command::Completion;
        if (args.size() >= 2) {
            result.completion_shell = args[1];
        }
        return result;
    }
    result.command = Command::Unknown;
    result.unrecognized = first;
    return result;
}

int execute_command(const ParseResult& result, std::ostream& out, std::ostream& err) {
    switch (result.command) {
        case Command::Help: {
            if (result.help_topic.empty()) {
                out << build_usage();
                return 0;
            }
            const CommandSpec* spec = find_spec(result.help_topic);
            if (!spec) {
                err << "forge: no help topic '" << result.help_topic << "'\n";
                return 1;
            }
            out << "forge " << spec->name;
            if (!spec->usage_args.empty()) {
                out << ' ' << spec->usage_args;
            }
            out << "\n\n  " << spec->summary << '\n';
            return 0;
        }
        case Command::Completion: {
            if (result.completion_shell != "bash") {
                err << "forge: unsupported shell '" << result.completion_shell
                    << "' (only 'bash' is supported)\n";
                return 1;
            }
            out << bash_completion_script();
            return 0;
        }
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
        case Command::Unknown: {
            err << "forge: '" << result.unrecognized << "' is not a forge command\n";
            if (const std::optional<std::string_view> suggestion = suggest_command(result.unrecognized)) {
                err << "       did you mean '" << *suggestion << "'?\n";
            }
            err << build_usage();
            return 1;
        }
    }
    return 1;
}

int run_interactive(std::istream& in, std::ostream& out, std::ostream& err) {
    out << "Forge interactive mode. Type a command (e.g. 'status', 'add <path>', 'commit -m "
           "<message>'), 'help' for the full list, or 'quit' to exit.\n";

    std::string line;
    while (true) {
        out << "forge> ";
        if (!std::getline(in, line)) {
            break;
        }
        const std::vector<std::string> tokens = tokenize_line(line);
        if (tokens.empty()) {
            continue;
        }
        if (tokens.front() == "quit" || tokens.front() == "exit") {
            break;
        }

        const CommandSpec* spec = find_spec(tokens.front());
        if (spec && spec->confirm_in_interactive_mode) {
            out << "Run '" << line << "'? [y/N] ";
            std::string answer;
            if (!std::getline(in, answer) || !(answer == "y" || answer == "Y" || answer == "yes")) {
                out << "Cancelled.\n";
                continue;
            }
        }

        const ParseResult parsed = parse_args(tokens);
        execute_command(parsed, out, err);

        const std::string_view hint = next_hint(parsed.command);
        if (!hint.empty()) {
            out << "Next: " << hint << '\n';
        }
    }
    return 0;
}

int run(const std::vector<std::string>& args, std::istream& in, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        return run_interactive(in, out, err);
    }
    return execute_command(parse_args(args), out, err);
}

int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    return run(args, std::cin, out, err);
}

} // namespace forge::cli

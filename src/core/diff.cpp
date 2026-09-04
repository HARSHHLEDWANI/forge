#include "core/diff.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

#include "core/error.hpp"
#include "core/object_encoding.hpp"
#include "storage/repository.hpp"

namespace forge::core {

namespace {

bool is_executable(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::perms perms = std::filesystem::status(path, ec).permissions();
    if (ec) {
        return false;
    }
    return (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
}

std::string read_file_content(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ForgeError("failed to read file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string repo_relative_posix(const std::filesystem::path& repo_root, const std::filesystem::path& path) {
    if (path == repo_root) {
        return "";
    }
    return std::filesystem::relative(path, repo_root).generic_string();
}

ObjectId hash_blob_content(std::string_view content) {
    return ObjectId::of(encode_canonical_object("blob", content));
}

void walk_working_tree(
    const IgnoreRules& ignore_rules, const std::filesystem::path& repo_root, const std::filesystem::path& path,
    Index& out) {
    std::error_code exists_ec;
    if (!std::filesystem::exists(std::filesystem::symlink_status(path, exists_ec)) || exists_ec) {
        return;
    }
    if (path.filename() == storage::kForgeDirName) {
        return;
    }

    const std::string repo_path = repo_relative_posix(repo_root, path);
    const bool is_symlink = std::filesystem::is_symlink(path);
    const bool is_dir = !is_symlink && std::filesystem::is_directory(path);

    if (!repo_path.empty() && ignore_rules.is_ignored(repo_path, is_dir)) {
        return;
    }

    if (is_dir) {
        for (const auto& child : std::filesystem::directory_iterator(path)) {
            walk_working_tree(ignore_rules, repo_root, child.path(), out);
        }
        return;
    }

    if (is_symlink) {
        const std::string target = std::filesystem::read_symlink(path).generic_string();
        out.upsert(IndexEntry{repo_path, EntryMode::Symlink, hash_blob_content(target)});
    } else if (std::filesystem::is_regular_file(path)) {
        const EntryMode mode = is_executable(path) ? EntryMode::ExecutableFile : EntryMode::RegularFile;
        out.upsert(IndexEntry{repo_path, mode, hash_blob_content(read_file_content(path))});
    }
    // Other file types (sockets, FIFOs, device files): not meaningful
    // content, silently skipped — same policy as staging.cpp.
}

bool looks_binary(std::string_view content) {
    constexpr std::size_t kScanLimit = 8000; // Git's own heuristic window
    return content.substr(0, kScanLimit).find('\0') != std::string_view::npos;
}

std::vector<std::string_view> split_lines(std::string_view content) {
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos < content.size()) {
        const std::size_t newline = content.find('\n', pos);
        if (newline == std::string_view::npos) {
            lines.push_back(content.substr(pos));
            break;
        }
        lines.push_back(content.substr(pos, newline - pos));
        pos = newline + 1;
    }
    return lines;
}

// Longest-common-subsequence edit script: standard O(N*M) DP table plus
// a greedy backtrack that prefers keeping deletions before insertions
// when both are equally valid, giving a deterministic, minimal script.
std::vector<DiffLine> lcs_diff(const std::vector<std::string_view>& a, const std::vector<std::string_view>& b) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();

    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            dp[i][j] = a[i] == b[j] ? dp[i + 1][j + 1] + 1 : std::max(dp[i + 1][j], dp[i][j + 1]);
        }
    }

    std::vector<DiffLine> result;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < n && j < m) {
        if (a[i] == b[j]) {
            result.push_back(DiffLine{LineOp::Context, std::string(a[i])});
            ++i;
            ++j;
        } else if (dp[i + 1][j] >= dp[i][j + 1]) {
            result.push_back(DiffLine{LineOp::Delete, std::string(a[i])});
            ++i;
        } else {
            result.push_back(DiffLine{LineOp::Insert, std::string(b[j])});
            ++j;
        }
    }
    while (i < n) {
        result.push_back(DiffLine{LineOp::Delete, std::string(a[i])});
        ++i;
    }
    while (j < m) {
        result.push_back(DiffLine{LineOp::Insert, std::string(b[j])});
        ++j;
    }
    return result;
}

} // namespace

std::vector<EntryChange> diff_index(const Index& old_state, const Index& new_state) {
    std::set<std::string> paths;
    for (const IndexEntry& entry : old_state.entries()) {
        paths.insert(entry.path);
    }
    for (const IndexEntry& entry : new_state.entries()) {
        paths.insert(entry.path);
    }

    std::vector<EntryChange> changes;
    for (const std::string& path : paths) {
        const std::optional<IndexEntry> old_entry = old_state.find(path);
        const std::optional<IndexEntry> new_entry = new_state.find(path);

        if (old_entry && new_entry && old_entry->mode == new_entry->mode && old_entry->blob_id == new_entry->blob_id) {
            continue; // unchanged
        }

        EntryChange change;
        change.path = path;
        change.old_mode = old_entry ? std::optional(old_entry->mode) : std::nullopt;
        change.old_blob_id = old_entry ? std::optional(old_entry->blob_id) : std::nullopt;
        change.new_mode = new_entry ? std::optional(new_entry->mode) : std::nullopt;
        change.new_blob_id = new_entry ? std::optional(new_entry->blob_id) : std::nullopt;
        change.type = !old_entry ? ChangeType::Added : (!new_entry ? ChangeType::Deleted : ChangeType::Modified);
        changes.push_back(std::move(change));
    }
    return changes;
}

Index snapshot_working_tree(const IgnoreRules& ignore_rules, const std::filesystem::path& repo_root) {
    Index result;
    walk_working_tree(ignore_rules, repo_root, repo_root, result);
    return result;
}

BlobDiff diff_blob_content(std::string_view old_content, std::string_view new_content) {
    if (looks_binary(old_content) || looks_binary(new_content)) {
        return BlobDiff{true, {}};
    }
    return BlobDiff{false, lcs_diff(split_lines(old_content), split_lines(new_content))};
}

std::string render_unified_diff(std::string_view path, const BlobDiff& diff) {
    if (diff.binary) {
        return "Binary files a/" + std::string(path) + " and b/" + std::string(path) + " differ\n";
    }
    const bool any_change =
        std::any_of(diff.lines.begin(), diff.lines.end(), [](const DiffLine& l) { return l.op != LineOp::Context; });
    if (!any_change) {
        return "";
    }

    std::size_t old_count = 0;
    std::size_t new_count = 0;
    for (const DiffLine& line : diff.lines) {
        if (line.op != LineOp::Insert) {
            ++old_count;
        }
        if (line.op != LineOp::Delete) {
            ++new_count;
        }
    }

    std::ostringstream out;
    out << "--- a/" << path << '\n';
    out << "+++ b/" << path << '\n';
    out << "@@ -1," << old_count << " +1," << new_count << " @@\n";
    for (const DiffLine& line : diff.lines) {
        const char marker = line.op == LineOp::Insert ? '+' : (line.op == LineOp::Delete ? '-' : ' ');
        out << marker << line.text << '\n';
    }
    return out.str();
}

std::vector<FileDiff> diff_working_tree(
    const storage::ObjectStore& objects, const Index& index, const IgnoreRules& ignore_rules,
    const std::filesystem::path& repo_root) {
    const Index working_snapshot = snapshot_working_tree(ignore_rules, repo_root);

    std::vector<FileDiff> result;
    for (const EntryChange& change : diff_index(index, working_snapshot)) {
        if (change.type == ChangeType::Added) {
            continue; // untracked: not part of `diff`
        }

        const std::string old_content = change.old_blob_id ? objects.get_blob(*change.old_blob_id).content : "";
        const std::string new_content =
            change.type == ChangeType::Deleted ? "" : read_file_content(repo_root / change.path);

        result.push_back(FileDiff{change.path, change.type, diff_blob_content(old_content, new_content)});
    }
    return result;
}

} // namespace forge::core

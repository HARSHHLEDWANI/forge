#include "core/tree.hpp"

#include <algorithm>

#include "core/error.hpp"

namespace forge::core {

std::string_view to_mode_string(EntryMode mode) noexcept {
    switch (mode) {
        case EntryMode::RegularFile: return "100644";
        case EntryMode::ExecutableFile: return "100755";
        case EntryMode::Directory: return "040000";
        case EntryMode::Symlink: return "120000";
    }
    return "000000";
}

std::optional<EntryMode> parse_mode_string(std::string_view mode) {
    if (mode == "100644") return EntryMode::RegularFile;
    if (mode == "100755") return EntryMode::ExecutableFile;
    if (mode == "040000") return EntryMode::Directory;
    if (mode == "120000") return EntryMode::Symlink;
    return std::nullopt;
}

namespace {

void check_no_duplicate_names(const std::vector<TreeEntry>& entries) {
    std::vector<std::string_view> names;
    names.reserve(entries.size());
    for (const auto& entry : entries) {
        names.push_back(entry.name);
    }
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) {
        throw ForgeError("tree has duplicate entry name");
    }
}

} // namespace

Tree::Tree(std::vector<TreeEntry> entries) : entries_(std::move(entries)) {
    check_no_duplicate_names(entries_);
}

std::string Tree::encode() const {
    std::vector<TreeEntry> sorted = entries_;
    std::sort(sorted.begin(), sorted.end(), [](const TreeEntry& a, const TreeEntry& b) {
        return a.name < b.name;
    });

    std::string payload;
    for (const auto& entry : sorted) {
        payload.append(to_mode_string(entry.mode));
        payload.push_back(' ');
        payload.append(entry.id.to_hex());
        payload.push_back(' ');
        payload.append(entry.name);
        payload.push_back('\n');
    }
    return payload;
}

std::optional<Tree> decode_tree(std::string_view canonical_payload) {
    std::vector<TreeEntry> entries;
    std::size_t pos = 0;

    while (pos < canonical_payload.size()) {
        const std::size_t line_end = canonical_payload.find('\n', pos);
        if (line_end == std::string_view::npos) {
            return std::nullopt; // trailing bytes without a terminating newline
        }
        const std::string_view line = canonical_payload.substr(pos, line_end - pos);
        pos = line_end + 1;

        const std::size_t first_space = line.find(' ');
        if (first_space == std::string_view::npos) {
            return std::nullopt;
        }
        const std::size_t second_space = line.find(' ', first_space + 1);
        if (second_space == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view mode_str = line.substr(0, first_space);
        const std::string_view hex_id = line.substr(first_space + 1, second_space - first_space - 1);
        const std::string_view name = line.substr(second_space + 1);

        const std::optional<EntryMode> mode = parse_mode_string(mode_str);
        if (!mode) {
            return std::nullopt;
        }
        const std::optional<ObjectId> id = ObjectId::parse(hex_id);
        if (!id) {
            return std::nullopt;
        }
        if (name.empty() || name.find('/') != std::string_view::npos) {
            return std::nullopt;
        }

        entries.push_back(TreeEntry{std::string(name), *mode, *id});
    }

    try {
        return Tree(std::move(entries));
    } catch (const ForgeError&) {
        return std::nullopt; // duplicate entry name: corrupted tree bytes
    }
}

} // namespace forge::core

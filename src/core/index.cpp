#include "core/index.hpp"

#include <algorithm>

#include "core/error.hpp"

namespace forge::core {

namespace {

void check_no_duplicate_paths(const std::vector<IndexEntry>& entries) {
    std::vector<std::string_view> paths;
    paths.reserve(entries.size());
    for (const auto& entry : entries) {
        paths.push_back(entry.path);
    }
    std::sort(paths.begin(), paths.end());
    if (std::adjacent_find(paths.begin(), paths.end()) != paths.end()) {
        throw ForgeError("index has duplicate entry path");
    }
}

} // namespace

Index::Index(std::vector<IndexEntry> entries) : entries_(std::move(entries)) {
    check_no_duplicate_paths(entries_);
}

std::optional<IndexEntry> Index::find(std::string_view path) const {
    for (const auto& entry : entries_) {
        if (entry.path == path) {
            return entry;
        }
    }
    return std::nullopt;
}

void Index::upsert(IndexEntry entry) {
    for (auto& existing : entries_) {
        if (existing.path == entry.path) {
            existing = std::move(entry);
            return;
        }
    }
    entries_.push_back(std::move(entry));
}

bool Index::remove(std::string_view path) {
    const auto it =
        std::find_if(entries_.begin(), entries_.end(), [&](const IndexEntry& e) { return e.path == path; });
    if (it == entries_.end()) {
        return false;
    }
    entries_.erase(it);
    return true;
}

std::string Index::encode() const {
    std::vector<IndexEntry> sorted = entries_;
    std::sort(sorted.begin(), sorted.end(), [](const IndexEntry& a, const IndexEntry& b) {
        return a.path < b.path;
    });

    std::string payload;
    for (const auto& entry : sorted) {
        payload.append(to_mode_string(entry.mode));
        payload.push_back(' ');
        payload.append(entry.blob_id.to_hex());
        payload.push_back(' ');
        payload.append(entry.path);
        payload.push_back('\n');
    }
    return payload;
}

std::optional<Index> decode_index(std::string_view canonical_bytes) {
    std::vector<IndexEntry> entries;
    std::size_t pos = 0;

    while (pos < canonical_bytes.size()) {
        const std::size_t line_end = canonical_bytes.find('\n', pos);
        if (line_end == std::string_view::npos) {
            return std::nullopt; // trailing bytes without a terminating newline
        }
        const std::string_view line = canonical_bytes.substr(pos, line_end - pos);
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
        const std::string_view path = line.substr(second_space + 1);

        const std::optional<EntryMode> mode = parse_mode_string(mode_str);
        if (!mode || *mode == EntryMode::Directory) {
            return std::nullopt; // the index is flat: a Directory entry is never valid
        }
        const std::optional<ObjectId> id = ObjectId::parse(hex_id);
        if (!id) {
            return std::nullopt;
        }
        if (path.empty()) {
            return std::nullopt;
        }

        entries.push_back(IndexEntry{std::string(path), *mode, *id});
    }

    try {
        return Index(std::move(entries));
    } catch (const ForgeError&) {
        return std::nullopt; // duplicate path: corrupted index bytes
    }
}

} // namespace forge::core

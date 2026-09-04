#include "core/commit.hpp"

#include <charconv>

namespace forge::core {

namespace {

constexpr std::string_view kTreePrefix = "tree ";
constexpr std::string_view kParentPrefix = "parent ";
constexpr std::string_view kAuthorPrefix = "author ";
constexpr std::string_view kTimestampPrefix = "timestamp ";

// Consumes one line (up to and including its '\n') from `remaining`.
// nullopt means `remaining` has no more newlines left to terminate a line.
std::optional<std::string_view> take_line(std::string_view& remaining) {
    const std::size_t newline = remaining.find('\n');
    if (newline == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view line = remaining.substr(0, newline);
    remaining.remove_prefix(newline + 1);
    return line;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

} // namespace

std::string encode_commit(const Commit& commit) {
    std::string payload;
    payload.append(kTreePrefix);
    payload.append(commit.tree_id.to_hex());
    payload.push_back('\n');
    for (const ObjectId& parent : commit.parent_ids) {
        payload.append(kParentPrefix);
        payload.append(parent.to_hex());
        payload.push_back('\n');
    }
    payload.append(kAuthorPrefix);
    payload.append(commit.author);
    payload.push_back('\n');
    payload.append(kTimestampPrefix);
    payload.append(std::to_string(commit.timestamp));
    payload.push_back('\n');
    payload.push_back('\n'); // blank line separates metadata from the message
    payload.append(commit.message);
    return payload;
}

std::optional<Commit> decode_commit(std::string_view canonical_payload) {
    std::string_view remaining = canonical_payload;

    const std::optional<std::string_view> tree_line = take_line(remaining);
    if (!tree_line || !starts_with(*tree_line, kTreePrefix)) {
        return std::nullopt;
    }
    const std::optional<ObjectId> tree_id = ObjectId::parse(tree_line->substr(kTreePrefix.size()));
    if (!tree_id) {
        return std::nullopt;
    }

    std::vector<ObjectId> parents;
    std::optional<std::string_view> line = take_line(remaining);
    while (line && starts_with(*line, kParentPrefix)) {
        const std::optional<ObjectId> parent_id = ObjectId::parse(line->substr(kParentPrefix.size()));
        if (!parent_id) {
            return std::nullopt;
        }
        parents.push_back(*parent_id);
        line = take_line(remaining);
    }

    if (!line || !starts_with(*line, kAuthorPrefix)) {
        return std::nullopt;
    }
    const std::string author(line->substr(kAuthorPrefix.size()));

    const std::optional<std::string_view> timestamp_line = take_line(remaining);
    if (!timestamp_line || !starts_with(*timestamp_line, kTimestampPrefix)) {
        return std::nullopt;
    }
    const std::string_view timestamp_field = timestamp_line->substr(kTimestampPrefix.size());
    std::int64_t timestamp = 0;
    const auto parse_result = std::from_chars(
        timestamp_field.data(), timestamp_field.data() + timestamp_field.size(), timestamp);
    if (parse_result.ec != std::errc() || parse_result.ptr != timestamp_field.data() + timestamp_field.size()) {
        return std::nullopt;
    }

    const std::optional<std::string_view> blank_line = take_line(remaining);
    if (!blank_line || !blank_line->empty()) {
        return std::nullopt;
    }

    return Commit{*tree_id, std::move(parents), author, timestamp, std::string(remaining)};
}

} // namespace forge::core

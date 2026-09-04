#include "core/log.hpp"

#include <optional>

namespace forge::core {

std::vector<LogEntry> commit_log(const storage::ObjectStore& objects, const ObjectId& start) {
    std::vector<LogEntry> entries;

    std::optional<ObjectId> current = start;
    while (current) {
        Commit commit = objects.get_commit(*current);
        const ObjectId id = *current;
        current = commit.parent_ids.empty() ? std::nullopt : std::make_optional(commit.parent_ids.front());
        entries.push_back(LogEntry{id, std::move(commit)});
    }
    return entries;
}

} // namespace forge::core

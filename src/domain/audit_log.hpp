#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace forge::domain {

// An append-only, durable trail of security-relevant events (user
// created, login succeeded/failed, token issued/revoked, permission
// granted, push accepted/rejected, ...). Append-only rather than
// atomic-replace-on-write like config/index/refs (see
// storage/atomic_file.hpp): an audit log's whole point is accumulating
// history, not maintaining a single current value, so each record()
// call opens in append mode and syncs just that write rather than
// rewriting the whole file.
class AuditLog {
public:
    explicit AuditLog(std::filesystem::path log_path);

    // Appends one line: "<unix-seconds> <actor> <action> <details>\n",
    // flushed and synced before returning — a record lost to a crash
    // between write and sync defeats the log's own purpose, so this
    // gets the same write-then-sync durability write_file_atomic gives
    // ordinary files. `details` is free text and always comes last
    // (never itself parsed back out), same convention as
    // core/commit.hpp's commit message. Throws core::ForgeError on any
    // I/O failure.
    void record(std::int64_t timestamp, std::string_view actor, std::string_view action, std::string_view details);

private:
    std::filesystem::path log_path_;
};

} // namespace forge::domain

#include "domain/audit_log.hpp"

#include <cstdio>
#include <sstream>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "core/error.hpp"

namespace forge::domain {

namespace {

void sync_file_handle(std::FILE* file) {
#if defined(_WIN32)
    const int result = _commit(_fileno(file));
#else
    const int result = fsync(fileno(file));
#endif
    if (result != 0) {
        throw core::ForgeError("failed to sync audit log to disk");
    }
}

// Replaces newlines with spaces so one log record is always exactly one
// line — `details` is free text (see audit_log.hpp) and could otherwise
// let a caller inject fake extra log lines.
std::string sanitize_field(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return result;
}

} // namespace

AuditLog::AuditLog(std::filesystem::path log_path) : log_path_(std::move(log_path)) {}

void AuditLog::record(
    std::int64_t timestamp, std::string_view actor, std::string_view action, std::string_view details) {
    std::ostringstream line;
    line << timestamp << ' ' << sanitize_field(actor) << ' ' << sanitize_field(action) << ' '
         << sanitize_field(details) << '\n';
    const std::string rendered = line.str();

    std::filesystem::create_directories(log_path_.parent_path());
    std::FILE* file = std::fopen(log_path_.string().c_str(), "ab");
    if (file == nullptr) {
        throw core::ForgeError("failed to open audit log: " + log_path_.string());
    }
    const std::size_t written = std::fwrite(rendered.data(), 1, rendered.size(), file);
    if (written != rendered.size()) {
        std::fclose(file);
        throw core::ForgeError("short write to audit log: " + log_path_.string());
    }
    if (std::fflush(file) != 0) {
        std::fclose(file);
        throw core::ForgeError("failed to flush audit log: " + log_path_.string());
    }
    sync_file_handle(file);
    std::fclose(file);
}

} // namespace forge::domain

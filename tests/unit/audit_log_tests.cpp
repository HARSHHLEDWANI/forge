#include <fstream>
#include <sstream>

#include "domain/audit_log.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::domain::AuditLog;
using forge::test::TempDir;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

FORGE_TEST_CASE(audit_log_appends_one_line_per_record) {
    TempDir dir;
    const std::filesystem::path log_path = dir.path() / "audit.log";
    AuditLog log(log_path);

    log.record(1000, "alice", "login_succeeded", "");
    log.record(1001, "bob", "token_issued", "pat abc123");

    const std::string content = read_file(log_path);
    FORGE_CHECK(content.find("1000 alice login_succeeded") != std::string::npos);
    FORGE_CHECK(content.find("1001 bob token_issued pat abc123") != std::string::npos);

    std::size_t line_count = 0;
    for (char c : content) {
        if (c == '\n') {
            ++line_count;
        }
    }
    FORGE_CHECK(line_count == 2);
}

FORGE_TEST_CASE(audit_log_creates_its_parent_directory) {
    TempDir dir;
    const std::filesystem::path log_path = dir.path() / "nested" / "audit.log";
    AuditLog log(log_path);
    log.record(1000, "alice", "user_created", "");
    FORGE_CHECK(std::filesystem::exists(log_path));
}

FORGE_TEST_CASE(audit_log_sanitizes_newlines_in_free_text_fields) {
    TempDir dir;
    const std::filesystem::path log_path = dir.path() / "audit.log";
    AuditLog log(log_path);

    log.record(1000, "alice", "note", "line one\nfake extra line\r\nanother");

    const std::string content = read_file(log_path);
    std::size_t line_count = 0;
    for (char c : content) {
        if (c == '\n') {
            ++line_count;
        }
    }
    FORGE_CHECK(line_count == 1); // the embedded newlines never split this into extra records
}

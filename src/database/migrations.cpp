#include "database/migrations.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "core/error.hpp"

namespace forge::database {

namespace {

struct MigrationFile {
    long version;
    std::filesystem::path path;
};

long parse_version_prefix(const std::string& filename) {
    const std::size_t underscore = filename.find('_');
    if (underscore == std::string::npos || underscore == 0) {
        throw core::ForgeError("migration filename must start with '<digits>_': " + filename);
    }
    const std::string digits = filename.substr(0, underscore);
    if (!std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw core::ForgeError("migration filename must start with '<digits>_': " + filename);
    }
    return std::stol(digits);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw core::ForgeError("failed to read migration file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

std::size_t apply_pending_migrations(PostgresConnection& connection, const std::filesystem::path& migrations_dir) {
    connection.exec(
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "  version BIGINT PRIMARY KEY,"
        "  applied_at TIMESTAMPTZ NOT NULL DEFAULT now()"
        ")");

    std::vector<MigrationFile> files;
    for (const auto& entry : std::filesystem::directory_iterator(migrations_dir)) {
        if (entry.path().extension() != ".sql") {
            continue;
        }
        files.push_back(MigrationFile{parse_version_prefix(entry.path().filename().string()), entry.path()});
    }
    std::sort(files.begin(), files.end(), [](const MigrationFile& a, const MigrationFile& b) {
        return a.version < b.version;
    });

    std::unordered_set<long> applied;
    for (const QueryRow& row : connection.exec("SELECT version FROM schema_migrations").rows) {
        applied.insert(std::stol(*row.at(0)));
    }

    std::size_t applied_count = 0;
    for (const MigrationFile& file : files) {
        if (applied.count(file.version) != 0) {
            continue;
        }

        Transaction transaction(connection);
        connection.exec_batch(read_file(file.path));
        connection.exec(
            "INSERT INTO schema_migrations (version) VALUES ($1)", {std::to_string(file.version)});
        transaction.commit();
        ++applied_count;
    }
    return applied_count;
}

} // namespace forge::database

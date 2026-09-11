#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "database/migrations.hpp"
#include "database/postgres_connection.hpp"
#include "server/app.hpp"
#include "transport/http_server.hpp"

namespace {

struct ServerArgs {
    std::string bind_address = "0.0.0.0";
    std::uint16_t port = 8080;
    std::filesystem::path repos_root = "forge-repos";
    std::filesystem::path data_root = "forge-data";
    std::string database_url; // empty: no PostgreSQL, collaboration routes/web UI stay off (see server/app.hpp)
    std::filesystem::path migrations_root = "migrations"; // only consulted when database_url is set
};

ServerArgs parse_server_args(const std::vector<std::string>& args) {
    ServerArgs result;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--port" && i + 1 < args.size()) {
            result.port = static_cast<std::uint16_t>(std::stoi(args[++i]));
        } else if (args[i] == "--bind" && i + 1 < args.size()) {
            result.bind_address = args[++i];
        } else if (args[i] == "--repos-dir" && i + 1 < args.size()) {
            result.repos_root = args[++i];
        } else if (args[i] == "--data-dir" && i + 1 < args.size()) {
            result.data_root = args[++i];
        } else if (args[i] == "--database-url" && i + 1 < args.size()) {
            result.database_url = args[++i];
        } else if (args[i] == "--migrations-dir" && i + 1 < args.size()) {
            result.migrations_root = args[++i];
        }
    }
    return result;
}

// FORGE_DATABASE_URL (an environment variable) takes priority over
// --database-url: a CLI argument is visible to any other local user via
// `ps`/`/proc/<pid>/cmdline` on a shared machine, and the connection
// string carries the database password. --database-url still works,
// with a warning, for local/dev convenience (exactly how this project's
// own docker-compose.yml and scripts/dev-build.ps1 workflows use it).
std::string resolve_database_url(const ServerArgs& parsed) {
    if (const char* env_value = std::getenv("FORGE_DATABASE_URL"); env_value != nullptr && *env_value != '\0') {
        return env_value;
    }
    if (!parsed.database_url.empty()) {
        std::cerr << "forge-server: warning: --database-url exposes the database password to any other "
                     "local user (e.g. via `ps`); prefer the FORGE_DATABASE_URL environment variable\n";
    }
    return parsed.database_url;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    ServerArgs parsed = parse_server_args(args);
    parsed.database_url = resolve_database_url(parsed);
    std::filesystem::create_directories(parsed.repos_root);
    std::filesystem::create_directories(parsed.data_root);

    if (!parsed.database_url.empty()) {
        // A fresh deployment's database starts with no schema at all;
        // applying migrations here (idempotently — see
        // apply_pending_migrations) means "point forge-server at a
        // database" is the whole setup step, rather than a separate
        // manual migration run every operator has to remember before
        // the collaboration routes/web UI will work.
        try {
            forge::database::PostgresConnection connection(parsed.database_url);
            const std::size_t applied =
                forge::database::apply_pending_migrations(connection, parsed.migrations_root);
            if (applied > 0) {
                std::cout << "forge-server: applied " << applied << " database migration(s)\n";
            }
        } catch (const forge::core::ForgeError& e) {
            std::cerr << "forge-server: migration failed: " << e.what() << '\n';
            return 1;
        }
    }

    forge::transport::HttpServer http_server(parsed.bind_address, parsed.port);
    forge::server::wire_routes(http_server, parsed.repos_root, parsed.data_root, parsed.database_url);

    try {
        http_server.start();
    } catch (const forge::core::ForgeError& e) {
        std::cerr << "forge-server: " << e.what() << '\n';
        return 1;
    }

    std::cout << "forge-server listening on " << parsed.bind_address << ":" << http_server.port() << '\n';
    http_server.serve();
    return 0;
}

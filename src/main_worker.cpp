#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/backup.hpp"
#include "core/error.hpp"
#include "core/verify.hpp"
#include "server/repo_registry.hpp"
#include "services/worker_pool.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"

namespace {

std::atomic<bool> g_shutdown_requested{false};

// Signal handlers may only touch async-signal-safe state — setting an
// atomic flag is the whole handler; the actual shutdown (joining worker
// threads, letting an in-flight job finish) happens on the main thread,
// which polls this flag below.
void handle_signal(int) { g_shutdown_requested.store(true); }

struct WorkerArgs {
    std::string database_url;
    std::filesystem::path repos_root = "forge-repos";
    std::filesystem::path backups_root = "forge-backups";
    std::size_t thread_count = 2;
};

WorkerArgs parse_worker_args(const std::vector<std::string>& args) {
    WorkerArgs result;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--database-url" && i + 1 < args.size()) {
            result.database_url = args[++i];
        } else if (args[i] == "--repos-dir" && i + 1 < args.size()) {
            result.repos_root = args[++i];
        } else if (args[i] == "--backups-dir" && i + 1 < args.size()) {
            result.backups_root = args[++i];
        } else if (args[i] == "--threads" && i + 1 < args.size()) {
            result.thread_count = static_cast<std::size_t>(std::stoul(args[++i]));
        }
    }
    return result;
}

// Same FORGE_DATABASE_URL-over-flag priority as forge-server
// (src/main_server.cpp) — see resolve_database_url() there for why.
std::string resolve_database_url(const WorkerArgs& parsed) {
    if (const char* env_value = std::getenv("FORGE_DATABASE_URL"); env_value != nullptr && *env_value != '\0') {
        return env_value;
    }
    if (!parsed.database_url.empty()) {
        std::cerr << "forge-worker: warning: --database-url exposes the database password to any other "
                     "local user (e.g. via `ps`); prefer the FORGE_DATABASE_URL environment variable\n";
    }
    return parsed.database_url;
}

// The "verify" job kind: runs core::verify_repository (Phase 11)
// against the repository named by the job's payload, throwing if it
// finds any real problem (corrupt or missing objects) — WorkerPool
// turns that throw into a retry-with-backoff, then a permanent failure
// once max_attempts is exhausted, the same as any other handler.
// Idempotent by construction: verify_repository never writes anything,
// so running it twice for the same job is exactly as safe as running
// it once (this phase's "Study: ... idempotency" in practice).
void handle_verify_job(const forge::database::DbJob& job, const std::filesystem::path& repos_root) {
    const forge::server::RepoRegistry registry(repos_root);
    if (!registry.exists(job.payload)) {
        throw forge::core::ForgeError("no such repository: " + job.payload);
    }
    const std::filesystem::path forge_dir = registry.forge_dir_for(job.payload);
    const forge::storage::RepositoryConfig config = forge::storage::load_config(forge_dir);
    const forge::storage::ObjectStore objects(config.storage_root);
    const forge::storage::RefStore refs(forge_dir);

    const forge::core::VerifyReport report = forge::core::verify_repository(objects, refs);
    if (!report.ok()) {
        std::ostringstream summary;
        summary << report.corrupt_objects.size() << " corrupt, " << report.missing_objects.size() << " missing";
        throw forge::core::ForgeError("verify failed for '" + job.payload + "': " + summary.str());
    }
}

// The "backup" job kind: runs core::create_backup (Phase 18) for the
// repository named by the job's payload, into
// `<backups_root>/<payload>/` — a fixed, single destination per repo,
// so re-running the same job (a retry, or a later scheduled backup) is
// exactly the incremental, resumable update create_backup() already
// documents, not a fresh full copy each time.
void handle_backup_job(
    const forge::database::DbJob& job, const std::filesystem::path& repos_root,
    const std::filesystem::path& backups_root) {
    const forge::server::RepoRegistry registry(repos_root);
    if (!registry.exists(job.payload)) {
        throw forge::core::ForgeError("no such repository: " + job.payload);
    }
    const std::filesystem::path forge_dir = registry.forge_dir_for(job.payload);
    const forge::storage::RepositoryConfig config = forge::storage::load_config(forge_dir);
    const forge::storage::ObjectStore objects(config.storage_root);
    const forge::storage::RefStore refs(forge_dir);

    forge::core::create_backup(objects, refs, backups_root / job.payload);
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    WorkerArgs parsed = parse_worker_args(args);
    parsed.database_url = resolve_database_url(parsed);
    if (parsed.database_url.empty()) {
        std::cerr << "forge-worker: --database-url or FORGE_DATABASE_URL is required\n";
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    forge::services::WorkerPool pool(parsed.database_url, parsed.thread_count);
    pool.register_handler("verify", [repos_root = parsed.repos_root](const forge::database::DbJob& job) {
        handle_verify_job(job, repos_root);
    });
    pool.register_handler(
        "backup", [repos_root = parsed.repos_root, backups_root = parsed.backups_root](const forge::database::DbJob& job) {
            handle_backup_job(job, repos_root, backups_root);
        });

    std::cout << "forge-worker starting with " << parsed.thread_count << " thread(s)\n";
    pool.start();

    while (!g_shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "forge-worker shutting down (waiting for in-flight jobs to finish)...\n";
    pool.stop();
    std::cout << "forge-worker stopped\n";
    return 0;
}

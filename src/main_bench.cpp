// forge-bench: measures the current object store's real performance
// characteristics so Phase 19 ("Only after benchmarks: compression,
// pack files, GC, object indexes" — implementation-plan.md) has actual
// numbers to decide from, instead of speculatively building any of
// those. See docs/adr/0005-storage-optimization-deferred.md for what
// this run's results led to.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

#include <cstdlib>
#include <thread>

#include "core/blob.hpp"
#include "core/branching.hpp"
#include "core/checkout.hpp"
#include "core/commit.hpp"
#include "core/committing.hpp"
#include "core/ignore_rules.hpp"
#include "core/staging.hpp"
#include "core/tree_builder.hpp"
#include "core/verify.hpp"
#include "database/postgres_connection.hpp"
#include "server/app.hpp"
#include "storage/index_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "transport/http_client.hpp"
#include "transport/http_server.hpp"

namespace {

using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string random_content(std::size_t size, std::mt19937_64& rng) {
    // Realistic-ish text content (not all-zero, which some naive
    // "compression helps" arguments over-index on) — printable ASCII
    // from a small alphabet, long enough runs to be somewhat
    // compressible like real source code, not incompressible random
    // bytes either.
    static constexpr std::string_view alphabet =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 \n";
    std::string out;
    out.reserve(size);
    std::uniform_int_distribution<std::size_t> pick(0, alphabet.size() - 1);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(alphabet[pick(rng)]);
    }
    return out;
}

void print_row(const std::string& label, double value, const std::string& unit) {
    std::cout << "  " << std::left << std::setw(38) << label << std::right << std::setw(12) << std::fixed
               << std::setprecision(2) << value << ' ' << unit << '\n';
}

// Write/read throughput at a given object size, N objects.
void bench_object_store_throughput(std::size_t object_size, int count) {
    std::cout << "\n--- ObjectStore throughput: " << count << " objects x " << object_size << " bytes ---\n";

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("forge-bench-objects-" + std::to_string(object_size));
    std::filesystem::remove_all(dir);
    forge::storage::ObjectStore store(dir);

    std::mt19937_64 rng{12345};
    std::vector<std::string> contents;
    contents.reserve(count);
    for (int i = 0; i < count; ++i) {
        contents.push_back(random_content(object_size, rng));
    }

    std::vector<forge::core::ObjectId> ids;
    ids.reserve(count);
    const Clock::time_point write_start = Clock::now();
    for (const std::string& content : contents) {
        ids.push_back(store.put_blob(forge::core::Blob{content}));
    }
    const double write_seconds = seconds_since(write_start);

    // Read back in a shuffled order, not insertion order, so this
    // reflects real access patterns (e.g. checkout touching files in a
    // different order than they were originally staged) rather than
    // whatever locality a monotonic write pattern happens to produce.
    std::shuffle(ids.begin(), ids.end(), rng);
    const Clock::time_point read_start = Clock::now();
    for (const forge::core::ObjectId& id : ids) {
        store.get_blob(id);
    }
    const double read_seconds = seconds_since(read_start);

    print_row("Write throughput", count / write_seconds, "objects/sec");
    print_row("Write throughput", (object_size * count / 1e6) / write_seconds, "MB/sec");
    print_row("Read throughput", count / read_seconds, "objects/sec");
    print_row("Read throughput", (object_size * count / 1e6) / read_seconds, "MB/sec");

    // Storage overhead: canonical encoding is "<type> <size>\0<payload>"
    // (core/object_encoding.hpp) — no compression. How much bigger is
    // that than the raw content, on average?
    std::uintmax_t total_raw = 0;
    std::uintmax_t total_on_disk = 0;
    for (std::size_t i = 0; i < contents.size(); ++i) {
        total_raw += contents[i].size();
        const std::string hex = ids[i].to_hex();
        const std::filesystem::path object_path = dir / hex.substr(0, 2) / hex.substr(2);
        std::error_code ec;
        total_on_disk += std::filesystem::file_size(object_path, ec);
    }
    print_row("Storage overhead", 100.0 * (double(total_on_disk) - double(total_raw)) / double(total_raw), "%");

    std::filesystem::remove_all(dir);
}

// Real git's rationale for a two-hex-char shard prefix (objects/xx/...)
// is avoiding one directory with millions of entries, which is slow to
// list/stat on most filesystems. Does this store's layout actually
// spread objects evenly across its 256 shards, or would a different
// number of prefix characters do meaningfully better?
void bench_shard_distribution(int count) {
    std::cout << "\n--- Shard distribution: " << count << " objects across 256 shards ---\n";

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "forge-bench-shards";
    std::filesystem::remove_all(dir);
    forge::storage::ObjectStore store(dir);

    std::mt19937_64 rng{999};
    for (int i = 0; i < count; ++i) {
        store.put_blob(forge::core::Blob{random_content(64, rng)});
    }

    std::vector<int> shard_counts;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_directory()) {
            continue;
        }
        int files_in_shard = 0;
        for (const auto& file : std::filesystem::directory_iterator(entry.path())) {
            (void)file;
            ++files_in_shard;
        }
        shard_counts.push_back(files_in_shard);
    }

    const int shard_dir_count = static_cast<int>(shard_counts.size());
    const int max_in_a_shard = shard_counts.empty() ? 0 : *std::max_element(shard_counts.begin(), shard_counts.end());
    const double mean = shard_counts.empty()
                             ? 0.0
                             : std::accumulate(shard_counts.begin(), shard_counts.end(), 0.0) / shard_counts.size();

    print_row("Shard directories used", shard_dir_count, "/ 256");
    print_row("Mean objects per shard", mean, "objects");
    print_row("Most-loaded shard", max_in_a_shard, "objects");

    std::filesystem::remove_all(dir);
}

// A synthetic "real repository" workload: commit N files, then run
// core::verify_repository's full reachability walk (Phase 11) — the
// operation whose cost scales with total object count, so it's the one
// most likely to justify an object index if the store's own filesystem
// lookup (path = hash prefix; the shard directory *is* the index)
// weren't already fast enough.
void bench_commit_and_verify(int file_count) {
    std::cout << "\n--- Commit + verify: " << file_count << " files ---\n";

    const std::filesystem::path repo_root = std::filesystem::temp_directory_path() / "forge-bench-repo";
    std::filesystem::remove_all(repo_root);
    forge::storage::initialize_repository(repo_root);
    const forge::storage::RepositoryConfig config = forge::storage::load_config(repo_root / ".forge");

    forge::storage::ObjectStore objects(config.storage_root);
    forge::storage::IndexStore index_store(repo_root / ".forge" / "index");
    forge::storage::RefStore refs(repo_root / ".forge");
    const forge::core::IgnoreRules ignore_rules = forge::core::IgnoreRules::parse("");

    std::mt19937_64 rng{42};
    for (int i = 0; i < file_count; ++i) {
        const std::filesystem::path path = repo_root / ("dir" + std::to_string(i / 100)) / ("file" + std::to_string(i) + ".txt");
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << random_content(200, rng);
    }

    const Clock::time_point stage_start = Clock::now();
    forge::core::stage_path(objects, index_store, ignore_rules, repo_root, repo_root);
    const double stage_seconds = seconds_since(stage_start);

    const Clock::time_point commit_start = Clock::now();
    forge::core::create_commit(objects, index_store, refs, "Bench <bench@example.com>", "bench commit", 1000);
    const double commit_seconds = seconds_since(commit_start);

    const Clock::time_point verify_start = Clock::now();
    const forge::core::VerifyReport report = forge::core::verify_repository(objects, refs);
    const double verify_seconds = seconds_since(verify_start);

    print_row("Stage time", stage_seconds, "sec");
    print_row("Commit time", commit_seconds, "sec");
    print_row("Verify (fsck) time", verify_seconds, "sec");
    print_row("Objects verified", static_cast<double>(report.objects_checked), "objects");

    std::filesystem::remove_all(repo_root);
}

// Phase 20 ("Only after measurement: caching, Redis, separate object
// storage, replication, horizontal scaling, consensus"): the concrete
// question that scope raises for *this* codebase specifically is
// whether HttpServer's single-threaded accept loop (a deliberate Phase
// 12 choice — see transport/http_server.hpp) is actually a bottleneck
// for how this tool is used. Measures sequential request throughput
// against a real running server — the honest ceiling of "one request at
// a time, no concurrency" as it exists today.
void bench_http_server_throughput(int request_count) {
    std::cout << "\n--- HTTP server throughput: " << request_count << " sequential GET /healthz ---\n";

    forge::transport::HttpServer server("127.0.0.1", 0);
    server.route("GET", "/healthz", [](const forge::transport::HttpRequest&) {
        return forge::transport::json_response(200, "OK", R"({"status":"ok"})");
    });
    server.start();
    std::thread server_thread([&server] { server.serve(); });

    forge::transport::HttpClientRequest request;
    request.method = "GET";
    request.path = "/healthz";

    const Clock::time_point start = Clock::now();
    for (int i = 0; i < request_count; ++i) {
        forge::transport::send_http_request("127.0.0.1", server.port(), request);
    }
    const double elapsed = seconds_since(start);

    server.stop();
    server_thread.join();

    print_row("Sequential throughput", request_count / elapsed, "requests/sec");
    print_row("Mean request latency", 1000.0 * elapsed / request_count, "ms");
}

// The collaboration API (server/collaboration_routes.cpp) opens a fresh
// PostgresConnection — a new TCP connection plus a new Postgres auth
// handshake — for every single HTTP request, rather than reusing one.
// Quantifies exactly what that costs versus running the same query on
// an already-open connection, so "should this be a persistent
// connection instead" has real numbers behind it instead of a guess.
// Only runs if FORGE_BENCH_DATABASE_URL is set — unlike the
// object-store benchmarks above, this one needs a live PostgreSQL to
// mean anything.
void bench_postgres_connection_overhead(int iterations) {
    const char* url = std::getenv("FORGE_BENCH_DATABASE_URL");
    if (url == nullptr || *url == '\0') {
        std::cout << "\n--- Postgres connection overhead: skipped (FORGE_BENCH_DATABASE_URL not set) ---\n";
        return;
    }
    std::cout << "\n--- Postgres connection overhead: " << iterations << " iterations ---\n";

    const Clock::time_point connect_start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        forge::database::PostgresConnection connection(url);
        connection.exec("SELECT 1");
    }
    const double connect_and_query_seconds = seconds_since(connect_start);

    forge::database::PostgresConnection persistent(url);
    const Clock::time_point query_start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        persistent.exec("SELECT 1");
    }
    const double query_only_seconds = seconds_since(query_start);

    print_row("New connection + query", 1000.0 * connect_and_query_seconds / iterations, "ms/request");
    print_row("Query on a reused connection", 1000.0 * query_only_seconds / iterations, "ms/request");
    print_row(
        "Connection setup overhead", 1000.0 * (connect_and_query_seconds - query_only_seconds) / iterations,
        "ms/request");
}

} // namespace

int main() {
    std::cout << "forge-bench: measuring real performance before Phases 19-20 optimize anything\n";

    bench_object_store_throughput(100, 2000);     // small objects: commit messages, short files
    bench_object_store_throughput(10 * 1024, 500); // medium: typical source files
    bench_object_store_throughput(1024 * 1024, 50); // large: the kind of file that'd actually benefit from compression

    bench_shard_distribution(5000);
    bench_commit_and_verify(2000);

    bench_http_server_throughput(500);
    bench_postgres_connection_overhead(200);

    std::cout << "\nDone. See docs/adr/0005-storage-optimization-deferred.md and "
                 "docs/adr/0006-scale-deferred.md for what these numbers mean.\n";
    return 0;
}

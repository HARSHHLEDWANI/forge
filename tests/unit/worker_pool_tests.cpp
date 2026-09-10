#include <atomic>
#include <chrono>
#include <thread>

#include "core/error.hpp"
#include "database/migrations.hpp"
#include "database/postgres_connection.hpp"
#include "database/repository_directory.hpp"
#include "services/worker_pool.hpp"
#include "support/postgres_test_support.hpp"

using forge::database::claim_next_pending_job;
using forge::database::DbJob;
using forge::database::enqueue_job;
using forge::services::exponential_backoff_seconds;
using forge::services::WorkerPool;

FORGE_TEST_CASE(exponential_backoff_seconds_doubles_each_attempt_up_to_the_cap) {
    FORGE_CHECK(exponential_backoff_seconds(0, 5, 300) == 5);
    FORGE_CHECK(exponential_backoff_seconds(1, 5, 300) == 10);
    FORGE_CHECK(exponential_backoff_seconds(2, 5, 300) == 20);
    FORGE_CHECK(exponential_backoff_seconds(3, 5, 300) == 40);
    FORGE_CHECK(exponential_backoff_seconds(20, 5, 300) == 300); // capped
}

FORGE_TEST_CASE(exponential_backoff_seconds_never_returns_less_than_zero_for_a_negative_attempts) {
    FORGE_CHECK(exponential_backoff_seconds(-1, 5, 300) == 5);
}

namespace {

// Polls `condition` until it's true or `timeout` elapses, returning
// whether it became true — used instead of a fixed sleep so these tests
// don't race a real worker thread's own polling interval.
template <typename Predicate>
bool wait_until(Predicate condition, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return condition();
}

} // namespace

FORGE_PG_TEST_CASE(worker_pool_runs_a_registered_handler_and_marks_the_job_done) {
    forge::database::PostgresConnection setup(pg_url);
    forge::database::apply_pending_migrations(setup, FORGE_MIGRATIONS_DIR);
    const std::int64_t job_id = enqueue_job(setup, "test-success", "payload");

    std::atomic<bool> handled{false};
    WorkerPool pool(pg_url, 1);
    pool.register_handler("test-success", [&](const DbJob&) { handled = true; });
    pool.start();

    FORGE_CHECK(wait_until([&] { return handled.load(); }, std::chrono::seconds(5)));
    FORGE_CHECK(wait_until(
        [&] {
            const auto result = setup.exec("SELECT status FROM jobs WHERE id = $1", {std::to_string(job_id)});
            return result.rows[0][0].value_or("") == "done";
        },
        std::chrono::seconds(5)));
    pool.stop();

    setup.exec("DELETE FROM jobs WHERE id = $1", {std::to_string(job_id)});
}

FORGE_PG_TEST_CASE(worker_pool_fails_a_job_permanently_once_max_attempts_is_exhausted) {
    forge::database::PostgresConnection setup(pg_url);
    forge::database::apply_pending_migrations(setup, FORGE_MIGRATIONS_DIR);
    const std::int64_t job_id = enqueue_job(setup, "test-always-fails", "payload", /*max_attempts=*/1);

    WorkerPool pool(pg_url, 1);
    pool.register_handler("test-always-fails", [](const DbJob&) { throw forge::core::ForgeError("boom"); });
    pool.start();

    FORGE_CHECK(wait_until(
        [&] {
            const auto result = setup.exec("SELECT status FROM jobs WHERE id = $1", {std::to_string(job_id)});
            return result.rows[0][0].value_or("") == "failed";
        },
        std::chrono::seconds(5)));
    pool.stop();

    const auto final_state = setup.exec("SELECT error FROM jobs WHERE id = $1", {std::to_string(job_id)});
    FORGE_CHECK(final_state.rows[0][0].value_or("").find("boom") != std::string::npos);

    setup.exec("DELETE FROM jobs WHERE id = $1", {std::to_string(job_id)});
}

FORGE_PG_TEST_CASE(worker_pool_fails_immediately_when_no_handler_is_registered_for_the_kind) {
    forge::database::PostgresConnection setup(pg_url);
    forge::database::apply_pending_migrations(setup, FORGE_MIGRATIONS_DIR);
    const std::int64_t job_id = enqueue_job(setup, "test-unknown-kind", "payload", /*max_attempts=*/5);

    WorkerPool pool(pg_url, 1); // no handlers registered at all
    pool.start();

    FORGE_CHECK(wait_until(
        [&] {
            const auto result = setup.exec("SELECT status, attempts FROM jobs WHERE id = $1", {std::to_string(job_id)});
            return result.rows[0][0].value_or("") == "failed";
        },
        std::chrono::seconds(5)));
    pool.stop();

    // Failed on the very first attempt, not after retrying 5 times —
    // a missing handler isn't a transient condition backoff ever fixes.
    const auto final_state = setup.exec("SELECT attempts FROM jobs WHERE id = $1", {std::to_string(job_id)});
    FORGE_CHECK(final_state.rows[0][0].value_or("") == "1");

    setup.exec("DELETE FROM jobs WHERE id = $1", {std::to_string(job_id)});
}

FORGE_PG_TEST_CASE(worker_pool_stop_waits_for_an_in_flight_job_to_finish) {
    forge::database::PostgresConnection setup(pg_url);
    forge::database::apply_pending_migrations(setup, FORGE_MIGRATIONS_DIR);
    const std::int64_t job_id = enqueue_job(setup, "test-slow", "payload");

    std::atomic<bool> handler_started{false};
    std::atomic<bool> handler_finished{false};
    WorkerPool pool(pg_url, 1);
    pool.register_handler("test-slow", [&](const DbJob&) {
        handler_started = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        handler_finished = true;
    });
    pool.start();

    FORGE_CHECK(wait_until([&] { return handler_started.load(); }, std::chrono::seconds(5)));
    pool.stop(); // must block until the in-flight handler above actually returns
    FORGE_CHECK(handler_finished.load());

    const auto final_state = setup.exec("SELECT status FROM jobs WHERE id = $1", {std::to_string(job_id)});
    FORGE_CHECK(final_state.rows[0][0].value_or("") == "done");

    setup.exec("DELETE FROM jobs WHERE id = $1", {std::to_string(job_id)});
}

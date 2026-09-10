#include "services/worker_pool.hpp"

#include <algorithm>
#include <chrono>

#include "core/error.hpp"
#include "database/postgres_connection.hpp"

namespace forge::services {

std::int64_t exponential_backoff_seconds(int attempts, std::int64_t base_seconds, std::int64_t max_seconds) {
    const int capped_attempts = std::clamp(attempts, 0, 30); // avoids overflow from an unreasonably large attempts count
    std::int64_t delay = base_seconds;
    for (int i = 0; i < capped_attempts; ++i) {
        if (delay >= max_seconds) {
            return max_seconds;
        }
        delay *= 2;
    }
    return std::min(delay, max_seconds);
}

WorkerPool::WorkerPool(std::string database_url, std::size_t thread_count)
    : database_url_(std::move(database_url)), thread_count_(thread_count) {}

WorkerPool::~WorkerPool() { stop(); }

void WorkerPool::register_handler(std::string_view kind, JobHandler handler) {
    handlers_[std::string(kind)] = std::move(handler);
}

void WorkerPool::start() {
    for (std::size_t i = 0; i < thread_count_; ++i) {
        threads_.emplace_back([this] { worker_loop(); });
    }
}

void WorkerPool::stop() {
    stop_requested_.store(true);
    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

void WorkerPool::worker_loop() {
    database::PostgresConnection connection(database_url_);

    while (!stop_requested_.load()) {
        std::optional<database::DbJob> job;
        try {
            job = database::claim_next_pending_job(connection);
        } catch (const core::ForgeError&) {
            // A transient DB hiccup claiming work isn't any particular
            // job's fault to retry-count against — just poll again
            // shortly.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        if (!job) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        const auto handler_it = handlers_.find(job->kind);
        if (handler_it == handlers_.end()) {
            try {
                // Force immediate permanent failure (no retry): a copy
                // with max_attempts capped to the current attempts count
                // makes reschedule_job_after_failure's own "attempts
                // exhausted" branch fire on the first try, reusing that
                // logic instead of a separate DAO entry point for what
                // is still fundamentally "this job failed".
                database::DbJob no_retry = *job;
                no_retry.max_attempts = job->attempts;
                database::reschedule_job_after_failure(
                    connection, no_retry, "no handler registered for kind '" + job->kind + "'", 0);
            } catch (const core::ForgeError&) {
                // Couldn't even record the failure — nothing more to do
                // without a live connection.
            }
            continue;
        }

        try {
            handler_it->second(*job);
            database::finish_job(connection, job->id);
        } catch (const std::exception& e) {
            try {
                database::reschedule_job_after_failure(
                    connection, *job, e.what(), exponential_backoff_seconds(job->attempts));
            } catch (const core::ForgeError&) {
                // Couldn't record the failure either — the job stays
                // 'running'; a future administrative pass has to notice
                // and requeue it, since this worker has no live
                // connection left to do that itself.
            }
        }
    }
}

} // namespace forge::services

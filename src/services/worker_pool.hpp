#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "database/repository_directory.hpp"

namespace forge::services {

// A job handler processes one claimed job and either returns normally
// (success) or throws (failure, triggering a retry with backoff — see
// WorkerPool's doc comment).
using JobHandler = std::function<void(const database::DbJob&)>;

// Exponential backoff with a cap: base_seconds * 2^attempts, capped at
// max_seconds — the standard "don't hammer a transiently-failing
// dependency" retry curve (implementation-plan.md Phase 17's "Study:
// producer/consumer and idempotency").
std::int64_t exponential_backoff_seconds(int attempts, std::int64_t base_seconds = 5, std::int64_t max_seconds = 300);

// A small pool of worker threads polling one Postgres-backed job queue
// (database/repository_directory.hpp's claim_next_pending_job). Each
// thread: claims a job, looks up a handler by the job's `kind`, runs
// it. On success, finish_job() marks it done. On a thrown exception,
// reschedule_job_after_failure() either requeues it with exponential
// backoff or marks it permanently failed once max_attempts is
// exhausted. A job whose `kind` has no registered handler fails
// immediately with no retries — a missing handler isn't a transient
// condition backoff would ever fix.
//
// Idempotency is the *handler's* responsibility, not this pool's: a
// retried job re-runs the handler from scratch, so every registered
// handler must be safe to run more than once for the same job (the
// producer/consumer half of this phase's study goal) — e.g. by making
// its underlying action naturally idempotent (like this project's own
// content-addressed object writes), rather than assuming the
// exactly-once delivery no "claim, maybe crash before finishing" queue
// can actually promise.
class WorkerPool {
public:
    WorkerPool(std::string database_url, std::size_t thread_count);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    // Must be called before start().
    void register_handler(std::string_view kind, JobHandler handler);

    void start();

    // Signals every worker thread to stop polling for new work and
    // blocks until each one finishes its current job (if any) and
    // exits — a worker never abandons a job mid-handler. Safe to call
    // more than once (including implicitly, via the destructor, if the
    // caller never called it).
    void stop();

private:
    void worker_loop();

    std::string database_url_;
    std::size_t thread_count_;
    std::map<std::string, JobHandler, std::less<>> handlers_;
    std::atomic<bool> stop_requested_{false};
    std::vector<std::thread> threads_;
};

} // namespace forge::services

-- Phase 17: retry/backoff bookkeeping for the job queue
-- (repository_directory.hpp's claim_next_pending_job already does the
-- SKIP LOCKED claiming; this adds the state a worker needs to retry a
-- failed job with backoff instead of giving up on the first failure,
-- or hammering it in a tight loop).

ALTER TABLE jobs
    ADD COLUMN attempts INT NOT NULL DEFAULT 0,
    ADD COLUMN max_attempts INT NOT NULL DEFAULT 5,
    ADD COLUMN next_attempt_at TIMESTAMPTZ NOT NULL DEFAULT now();

-- Replaces the Phase 15 partial index: a job is claimable once it's
-- pending *and* its backoff delay has elapsed.
DROP INDEX idx_jobs_status;
CREATE INDEX idx_jobs_claimable ON jobs (next_attempt_at) WHERE status IN ('pending', 'running');

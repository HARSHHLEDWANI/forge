-- Phase 15 initial schema. See architecture.md's "Database boundary":
-- PostgreSQL holds users, repositories, permissions, and job metadata —
-- git object content stays in the filesystem object store
-- (storage/object_store.hpp), never here.

CREATE TABLE users (
    id BIGSERIAL PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE repositories (
    id BIGSERIAL PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    owner_id BIGINT NOT NULL REFERENCES users(id),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- One row per repository, split from `repositories` itself so adding a
-- metadata field later never means migrating the identity/ownership
-- columns every foreign key here already points at.
CREATE TABLE repository_metadata (
    repository_id BIGINT PRIMARY KEY REFERENCES repositories(id) ON DELETE CASCADE,
    description TEXT NOT NULL DEFAULT '',
    default_branch TEXT NOT NULL DEFAULT 'main',
    is_private BOOLEAN NOT NULL DEFAULT false
);

CREATE TABLE repository_memberships (
    id BIGSERIAL PRIMARY KEY,
    repository_id BIGINT NOT NULL REFERENCES repositories(id) ON DELETE CASCADE,
    user_id BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role TEXT NOT NULL CHECK (role IN ('read', 'write', 'admin')),
    UNIQUE (repository_id, user_id)
);

-- Indexes the schema's own UNIQUE constraints don't already give us:
-- "every repo I have access to" (memberships by user) and "the pending
-- work" (jobs by status) are the two access patterns Phase 16/17 will
-- actually run.
CREATE INDEX idx_repository_memberships_user_id ON repository_memberships (user_id);

CREATE TABLE jobs (
    id BIGSERIAL PRIMARY KEY,
    kind TEXT NOT NULL,
    payload TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'running', 'done', 'failed')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    started_at TIMESTAMPTZ,
    finished_at TIMESTAMPTZ,
    error TEXT
);

CREATE INDEX idx_jobs_status ON jobs (status) WHERE status IN ('pending', 'running');

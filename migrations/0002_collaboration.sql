-- Phase 16: issues, pull requests, reviews, comments, labels, branch
-- protection. Comments are shared between issues and pull requests
-- (subject_type/subject_id) rather than two near-identical tables —
-- both are just "threaded text on a repository object", the same
-- "reuse over duplicate" call made throughout this codebase's C++ side.

CREATE TABLE issues (
    id BIGSERIAL PRIMARY KEY,
    repository_id BIGINT NOT NULL REFERENCES repositories(id) ON DELETE CASCADE,
    number BIGINT NOT NULL, -- per-repository sequential number, like GitHub's #123
    title TEXT NOT NULL,
    body TEXT NOT NULL DEFAULT '',
    author_id BIGINT NOT NULL REFERENCES users(id),
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'closed')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (repository_id, number)
);

CREATE TABLE pull_requests (
    id BIGSERIAL PRIMARY KEY,
    repository_id BIGINT NOT NULL REFERENCES repositories(id) ON DELETE CASCADE,
    number BIGINT NOT NULL,
    title TEXT NOT NULL,
    body TEXT NOT NULL DEFAULT '',
    author_id BIGINT NOT NULL REFERENCES users(id),
    source_branch TEXT NOT NULL,
    target_branch TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'merged', 'closed')),
    merge_commit TEXT, -- hex ObjectId, set only once status = 'merged'
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE (repository_id, number)
);

CREATE TABLE pull_request_reviews (
    id BIGSERIAL PRIMARY KEY,
    pull_request_id BIGINT NOT NULL REFERENCES pull_requests(id) ON DELETE CASCADE,
    reviewer_id BIGINT NOT NULL REFERENCES users(id),
    state TEXT NOT NULL CHECK (state IN ('approved', 'changes_requested', 'commented')),
    body TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE comments (
    id BIGSERIAL PRIMARY KEY,
    subject_type TEXT NOT NULL CHECK (subject_type IN ('issue', 'pull_request')),
    subject_id BIGINT NOT NULL, -- issues.id or pull_requests.id, per subject_type
    author_id BIGINT NOT NULL REFERENCES users(id),
    body TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX idx_comments_subject ON comments (subject_type, subject_id);

CREATE TABLE labels (
    id BIGSERIAL PRIMARY KEY,
    repository_id BIGINT NOT NULL REFERENCES repositories(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    color TEXT NOT NULL DEFAULT '#cccccc',
    UNIQUE (repository_id, name)
);

CREATE TABLE issue_labels (
    issue_id BIGINT NOT NULL REFERENCES issues(id) ON DELETE CASCADE,
    label_id BIGINT NOT NULL REFERENCES labels(id) ON DELETE CASCADE,
    PRIMARY KEY (issue_id, label_id)
);

-- One row per protected branch. `require_review`/`required_approvals`
-- are what POST /pulls/merge (server/app.cpp) checks before allowing a
-- merge into a protected branch; a *direct* push to one (bypassing a
-- PR) is rejected outright regardless of approvals — see the doc
-- comment on the push handler.
CREATE TABLE branch_protection_rules (
    id BIGSERIAL PRIMARY KEY,
    repository_id BIGINT NOT NULL REFERENCES repositories(id) ON DELETE CASCADE,
    branch_name TEXT NOT NULL,
    require_review BOOLEAN NOT NULL DEFAULT true,
    required_approvals INT NOT NULL DEFAULT 1,
    UNIQUE (repository_id, branch_name)
);

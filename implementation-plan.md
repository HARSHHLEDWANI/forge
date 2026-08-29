# Implementation Plan

## Phase 0 — Foundation
Build: CMake, C++20, warnings, tests, logging, error strategy, CLI skeleton.
Study: modern C++, CMake, testing, Git basics.
Exit: `forge --help`, `forge version`.

## Phase 1 — Filesystem
Build: repository discovery, `.forge`, config, safe paths, storage location, temp files, atomic replacement.
Study: filesystem semantics, file I/O, durability, crash consistency, path traversal.

## Phase 2 — Objects
Build: SHA-256, canonical serialization, ObjectId, parsing, validation.
Study: hashing, bytes, serialization, determinism.

## Phase 3 — Blob/Object Store
Build: Blob, ObjectStore, loose objects, streaming, duplicate detection, corruption verification.
Study: binary/streaming I/O and atomic publication.

## Phase 4 — Trees
Build: entries, modes/types, deterministic ordering, recursive tree creation.
Study: recursive structures, filesystem traversal, symlinks.

## Phase 5 — Index/Staging
Build: index format, add, add ., ignore rules, deletions, atomic index replacement.
Study: staging design and incremental scanning.

## Phase 6 — Commits/Refs
Build: commits, parents, metadata, HEAD, branches, atomic refs, commit/log/branch.
Study: DAGs, graph traversal, immutable data.

## Phase 7 — Checkout
Build: switch, checkout, detached HEAD, dirty-tree detection, safe overwrite prevention.
Study: state machines and transactional operations.

## Phase 8 — Diff
Build: working/index/tree diff, blob diff, binary handling.
Study: sequence comparison and Myers diff.

## Phase 9 — Merge
Build: merge-base, fast-forward, three-way merge, conflicts, merge commits.
Study: DAG algorithms and merge strategies.

## Phase 10 — CLI UX
Build: command registry, interactive menu, autocomplete, help, confirmations, clear errors.
Study: CLI architecture and terminal UX.

## Phase 11 — Reliability
Build: repository locks, crash recovery, verification, diagnostics.
Study: concurrency, locks, durability and recovery.
Test process crashes during writes and concurrent operations.

## Phase 12 — Server
Build: server executable, HTTP API, health checks, shared core.
Study: HTTP, sockets, request lifecycle and server concurrency.

## Phase 13 — Remotes
Build: clone, fetch, push, remote refs, object negotiation, non-fast-forward rejection, atomic ref updates.
Study: Git transport and optimistic concurrency.

## Phase 14 — Auth
Build: users, password KDF, SSH public keys, tokens, sessions, RBAC, audit logs.
Study: authentication, authorization and threat modeling.

## Phase 15 — PostgreSQL
Build: schema, migrations, users/repos/memberships/metadata/jobs.
Study: transactions, indexes, isolation, locking, DB/filesystem consistency.

## Phase 16 — Collaboration
Build: web UI, issues, PRs, reviews, comments, labels, branch protection.
Study: API and relational application design.

## Phase 17 — Workers
Build: queue, worker pool, retries/backoff, graceful shutdown.
Jobs: backups, GC, indexing, CI.
Study: producer/consumer and idempotency.

## Phase 18 — Backup/Recovery
Build: backup, verification, restore, restore tests.
Study: RPO/RTO and disaster recovery.

## Phase 19 — Storage Optimization
Only after benchmarks: compression, pack files, GC, object indexes.

## Phase 20 — Scale
Only after measurement: caching, Redis, separate object storage, replication, horizontal scaling, consensus where actually required.

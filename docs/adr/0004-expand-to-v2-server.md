# ADR 0004 — Expand Scope to V2 (Server, HTTP, Remotes, Auth, PostgreSQL)

Decision: proceed past V1 (frozen-scope.md) into V2 — server, HTTP API, remotes (clone/fetch/push), auth, and PostgreSQL-backed metadata — per explicit user direction to build the remaining phases (12-20) of implementation-plan.md.

V1's local Git engine (Phases 0-11) is complete and tested: objects, refs, index, commit/branch/checkout/diff/merge with conflict handling, interactive CLI, repository locks, crash recovery, and `forge verify`.

Implementation choices for this phase, also by explicit direction:

- **HTTP (Phase 12)**: hand-rolled sockets + a minimal HTTP/1.1 parser, no new dependency. Matches study-plan.md's networking goals and the project's existing from-scratch style (own test framework, own canonical object encoding, OpenSSL used only for the primitive SHA-256 itself).
- **PostgreSQL (Phase 15)**: a real PostgreSQL instance via docker-compose alongside the existing Dockerfile.dev toolchain, accessed through libpq. Matches architecture.md/data-model.md naming PostgreSQL explicitly and study-plan.md's transactions/isolation/locking learning goal — an abstracted/fake DB layer would risk not matching Postgres's real semantics and forcing rework later.

Reason for writing this down: frozen-scope.md's freeze rule requires an ADR for anything outside the current phase. This is that ADR.

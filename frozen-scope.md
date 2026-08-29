# Frozen Scope

## V1 — Local Git engine
Required:
- init
- configurable storage
- blob/tree/commit objects
- SHA-256
- canonical serialization
- content-addressed object store
- refs and HEAD
- index/staging
- add/status/commit/log/branch/switch/checkout/diff/merge
- three-way merge and conflicts
- `.forgeignore`
- safe paths
- atomic writes
- corruption detection
- crash recovery
- interactive + traditional CLI
- tests

Not V1:
- web UI
- users/accounts
- PostgreSQL
- HTTP/SSH
- clone/fetch/push
- PRs/issues/reviews
- CI/CD
- Redis
- Kubernetes
- replication/consensus
- cloud deployment
- pack files/advanced GC

## V2
Server, HTTP API, SSH, clone/fetch/push, auth, authorization, PostgreSQL metadata, concurrent ref updates.

## V3
Web UI, issues, PRs, reviews, comments, labels, branch protection.

## V4
Workers, backups, observability, GC, CI/artifacts.

## V5
Only measured scale requirements: caching, separate object storage, replication, consensus.

## Freeze rule
Anything outside the current phase requires an ADR and explicit scope change.

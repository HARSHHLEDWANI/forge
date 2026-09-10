# Benchmarks

`forge-bench` (src/main_bench.cpp, built alongside `forge`/`forge-server`/`forge-worker`) measures `storage::ObjectStore`'s real write/read throughput, storage overhead over raw content, shard-directory distribution, and combined stage/commit/verify timing on a synthetic multi-file repository.

Run it directly:

```sh
cmake --build build
./build/forge-bench
```

It's the evidence behind Phase 19's and Phase 20's decisions:

- `docs/adr/0005-storage-optimization-deferred.md` — compression, pack files, GC, object indexes.
- `docs/adr/0006-scale-deferred.md` — caching, Redis, separate object storage, replication, horizontal scaling, consensus, plus one concrete finding (per-request PostgreSQL connection overhead) worth tracking even though it isn't literally any of those.

Set `FORGE_BENCH_DATABASE_URL` to also run the PostgreSQL-dependent benchmark (skipped otherwise, the same "no database configured, no-op" convention `FORGE_TEST_DATABASE_URL` uses for tests — see tests/support/postgres_test_support.hpp).

Re-run `forge-bench` and update the relevant ADR if a future change might have shifted these numbers meaningfully (a different durability strategy, a different object layout, a connection-pooling change, etc.) — the point of a benchmark is that its conclusions expire when the thing it measured changes.

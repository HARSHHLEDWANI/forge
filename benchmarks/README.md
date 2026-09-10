# Benchmarks

`forge-bench` (src/main_bench.cpp, built alongside `forge`/`forge-server`/`forge-worker`) measures `storage::ObjectStore`'s real write/read throughput, storage overhead over raw content, shard-directory distribution, and combined stage/commit/verify timing on a synthetic multi-file repository.

Run it directly:

```sh
cmake --build build
./build/forge-bench
```

It's the evidence behind Phase 19's decision — see `docs/adr/0005-storage-optimization-deferred.md` for the numbers from the last run and what they say about compression, pack files, GC, and object indexes. Re-run it and update that ADR if a future change might have shifted these numbers meaningfully (a different durability strategy, a different object layout, etc.) — the point of a benchmark is that its conclusions expire when the thing it measured changes.

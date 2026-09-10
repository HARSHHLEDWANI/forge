# ADR 0005 — Storage Optimization (Phase 19): Deferred, With Numbers

Decision: implement none of compression, pack files, GC, or object indexes right now. implementation-plan.md's Phase 19 gates all four behind "only after benchmarks" — this ADR is the benchmark run and the reasoning it produced, per that gate.

## What was measured

`forge-bench` (src/main_bench.cpp) against this project's actual `storage::ObjectStore`, on the same Debian/gcc-13 dev container everything else in this repo is built and tested on:

```text
--- ObjectStore throughput: 2000 objects x 100 bytes ---
  Write throughput                            203.48 objects/sec
  Read throughput                           50640.53 objects/sec
  Storage overhead                              9.00 %

--- ObjectStore throughput: 500 objects x 10240 bytes ---
  Write throughput                            242.02 objects/sec
  Read throughput                             389.05 MB/sec
  Storage overhead                              0.11 %

--- ObjectStore throughput: 50 objects x 1048576 bytes ---
  Write throughput                             97.91 objects/sec (102.67 MB/sec)
  Read throughput                             343.56 MB/sec
  Storage overhead                              0.00 %

--- Shard distribution: 5000 objects across 256 shards ---
  Shard directories used                      256 / 256
  Mean objects per shard                       19.53 objects
  Most-loaded shard                            33 objects

--- Commit + verify: 2000 files ---
  Stage time                                    8.93 sec  (~224 files/sec)
  Commit time                                   0.15 sec
  Verify (fsck) time                            0.10 sec, 2022 objects
```

## What the numbers say, per optimization

- **Compression**: storage overhead over raw content is 9% for 100-byte objects (a fixed-size canonical-encoding header dominating a tiny payload — not something compression fixes), 0.11% at 10 KB, and unmeasurable at 1 MB. Real source files are large enough that the header cost is already negligible; compression would spend CPU to save space that isn't actually being wasted. Not justified.

- **Object indexes**: 5000 objects landed in all 256 shard directories with a tight spread (mean 19.5, max 33 per shard) — SHA-256's own uniform distribution already does what a purpose-built index would. `verify_repository`'s full reachability walk over 2022 objects took 0.10s, with no sign of scaling trouble the filesystem's own directory lookup isn't already handling. Not justified.

- **GC**: nothing in this codebase yet creates unreferenced objects — there's no branch deletion, no commit amend, no history rewrite (frozen-scope.md doesn't list any of those for V1 either). Garbage collection has nothing to collect. Not justified.

- **Pack files**: this is the one the numbers actually point at. Write throughput is ~100–240 objects/sec regardless of object size (100 B and 10 KB write at essentially the same objects/sec) — that's not a content-size-bound cost, it's a **per-object fixed cost**: `write_file_atomic` (storage/atomic_file.hpp) fsyncs the file and then the containing directory for every single object, by design (Phase 11's crash-consistency guarantee). Staging 2000 files takes 8.93s, essentially all of it in that per-object fsync cost. Pack files' real benefit here would be batching many objects behind one fsync — a genuine, numbers-backed future direction, *if* a real workload ever needs faster bulk writes than ~200 objects/sec. It doesn't yet: interactive local use (a handful of files per commit) never gets close to this limit, and 9 seconds for an initial 2000-file import is not a problem anyone has actually hit. Deferred, not dismissed — this is the concrete thing to revisit first if V1's usage ever changes.

## Reason

Matches this project's standing rule, stated everywhere from diff.hpp's O(N·M) LCS choice to storage.md's own "Future: compression, pack files, GC, object indexes": correctness and a working system first, optimize once a real measurement says to. The measurement now exists (`forge-bench`, runnable any time performance is in question) and it says: nothing here is currently a bottleneck for V1's actual usage pattern, except a specific, already-understood, fsync-bound write cost that only matters for bulk imports far larger than this tool's local, interactive use case produces today.

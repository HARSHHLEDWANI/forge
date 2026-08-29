# Testing Strategy

## Unit
Hashing, serialization, ObjectId, trees, commits, refs, merge-base, diff, ignore rules.

## Integration
Temporary repositories for init/add/commit/checkout/branch/merge/corruption/crash recovery.

## Concurrency
Concurrent commits, ref updates, object creation and stale pushes.

## Invariants
- deterministic serialization
- stable object IDs
- corruption fails verification
- identical blobs deduplicate
- historical commits remain readable
- ref updates are atomic

## Fuzzing
Eventually fuzz object/tree/commit/index/CLI parsers.

## Benchmarks
Benchmark hashing, add, commit, checkout, log, diff and merge before optimizing.

# Forge Agent Instructions

## Non-negotiable
- C++20.
- Build the core Git engine from scratch; do not shell out to `git`.
- Local mode works offline.
- Repository data uses configurable local HDD/SSD storage.
- Objects are immutable and content-addressed.
- References are mutable.
- Correctness and data integrity precede performance.
- Single-node/local comes before distributed architecture.

## Boundaries
CLI → Application Services → Forge Core → Storage Abstractions → Filesystem/DB.
CLI must not implement storage semantics directly.
Web UI must not access PostgreSQL/storage directly.

## C++ engineering
Use RAII, explicit ownership, `std::filesystem`, streaming I/O, modern STL and CMake. Avoid raw owning pointers and undefined behavior. Keep platform-specific code isolated.

## Git invariants
1. Object IDs derive from canonical bytes.
2. Objects never mutate.
3. Branches point to commits.
4. HEAD represents current checkout state.
5. Historical objects remain available while reachable.
6. Ref updates are atomic.
7. Never report success for an operation whose durable state is invalid.

## Storage safety
Use temporary writes + verification + atomic publication. Verify object hashes. Never trust arbitrary paths. Store repository-relative normalized paths.

## Security
Never log secrets. Never execute repository code during normal Git operations. CI eventually runs isolated. Authentication and authorization are separate.

## Scope
Before adding a feature read `frozen-scope.md`. Architecture-changing decisions require an ADR in `docs/adr/`.

## Definition of done
Specification + implementation + tests + failure-path review + documentation.

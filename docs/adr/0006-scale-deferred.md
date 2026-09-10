# ADR 0006 — Scale (Phase 20): Deferred, With Numbers

Decision: implement none of caching, Redis, separate object storage, replication, horizontal scaling, or consensus. implementation-plan.md's Phase 20 gates all of it behind "only after measurement, ... where actually required" — this ADR is that measurement, the same way ADR 0005 was for Phase 19's storage-optimization gate.

## What was measured

`forge-bench` (src/main_bench.cpp), extended for this phase with two more benchmarks against real running infrastructure:

```text
--- HTTP server throughput: 500 sequential GET /healthz ---
  Sequential throughput                      4902.00 requests/sec
  Mean request latency                          0.20 ms

--- Postgres connection overhead: 200 iterations ---
  New connection + query                       11.25 ms/request
  Query on a reused connection                   0.16 ms/request
  Connection setup overhead                     11.09 ms/request
```

## What the numbers say

- **HTTP throughput**: `transport::HttpServer`'s single-threaded accept loop (a deliberate Phase 12 choice, not yet revisited) handles ~4900 requests/second sequentially, sub-millisecond latency, on the same modest dev container everything else in this repo builds and tests on. This is a local, single-user tool with no measured production traffic at all. Nothing about Phase 20's list — caching, Redis, a separate object-storage tier, replication, horizontal scaling, or consensus — addresses a bottleneck that exists here; there's no load this ceiling has ever been close to, let alone exceeded. Not justified.

- **Consensus / replication**: this project has exactly one node, by design (ADR 0001's local-first stance; architecture.md's own "Principle: build a correct single-node system before distributed systems"). Consensus and replication solve problems — multiple authoritative copies disagreeing — that don't exist yet. Not justified, and premature: getting a wrong consensus protocol into a system before it needs one is a well-known way to add real bugs for a problem that isn't there yet.

- **Separate object storage**: `storage::ObjectStore` already isolates the object-storage concern behind a small interface (a directory root); moving it to something like S3 later is a swap behind that interface, not an architectural change that needs deciding now. Nothing here needs it before the filesystem itself becomes the bottleneck, and Phase 19's benchmarks showed it isn't one yet.

- **Caching / Redis**: nothing measured shows a repeated, expensive computation worth memoizing — reads are already fast (Phase 19's throughput numbers), and there's no multi-node deployment yet where a shared cache would even have a reader on the other end. Not justified.

## One real finding, tracked but not fixed here

The Postgres benchmark surfaced something concrete: `server/collaboration_routes.cpp` and `server/web_ui.cpp` open a **fresh `PostgresConnection` on every single HTTP request** rather than reusing one — and that costs ~11ms of the ~11.25ms total, i.e. the connection handshake is about 70x the cost of the query itself. Given `HttpServer`'s single-threaded design (one request handled at a time, no concurrent access to worry about), reusing one connection across requests for the collaboration/web-UI routes is a safe, contained, well-justified change.

It is **not implemented in this ADR's pass**. This is a request-latency detail surfaced by measuring for Phase 20, not one of the six things Phase 20 actually names (caching/Redis/object-storage/replication/horizontal-scaling/consensus) — none of which this finding is evidence for. Recorded here as the concrete next thing to fix if/when collaboration-API latency actually matters to someone, rather than folded into this already-large pass at the end of a long implementation run, where a broad refactor across every route is exactly the kind of change that deserves its own focused review.

## Reason

Same rule as ADR 0005: this project optimizes once a real measurement says to, not on the strength of what a "Scale" phase heading merely names as possible future work. The measurement exists now (`forge-bench`) and says: this is a correct single-node system with no observed load anywhere near any of its current ceilings, exactly the state architecture.md's own stated principle says to be in before distributed-systems concerns are worth taking on.

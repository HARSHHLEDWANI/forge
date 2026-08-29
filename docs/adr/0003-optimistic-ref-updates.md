# ADR 0003 — Optimistic Ref Updates

Decision: mutable refs use compare-and-update semantics.

A client supplies expected old ref A. If the current ref is still A, publish B. Otherwise reject the stale update.

Distributed consensus is reserved for a future multi-authority architecture.

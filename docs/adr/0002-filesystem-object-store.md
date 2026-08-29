# ADR 0002 — Filesystem Object Store

Decision: Git objects use a content-addressed filesystem store.

Reason: immutable objects map naturally to files and avoid turning repository content into relational metadata.

# Forge

A C++20 local-first, self-hosted Git/GitHub-like platform.

## Build order

Objects → storage → refs → index → add → commit → branch → checkout → diff → merge → CLI UX → reliability → server → clone/fetch/push → auth → PostgreSQL → collaboration → workers/backups → CI → scaling.

Local mode requires no internet.

Core rule: immutable content-addressed objects, mutable references, correctness before performance.

# Storage

Storage Abstractions layer (see `architecture.md`): filesystem-facing
primitives that Forge Core and the CLI build on, but never Git-domain
logic itself.

- `safe_path` — validates repository-relative paths against traversal
  and symlink escape.
- `atomic_file` — durable write-temp-verify-rename primitive.
- `repository` — `.forge` discovery and `init`.
- `object_store` — content-addressed loose-object storage (blobs for
  now; trees/commits reuse it starting Phase 4).

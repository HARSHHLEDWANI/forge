# Storage

Storage Abstractions layer (see `architecture.md`): filesystem-facing
primitives that Forge Core and the CLI build on, but never Git-domain
logic itself.

- `safe_path` — validates repository-relative paths against traversal
  and symlink escape.
- `atomic_file` — durable write-temp-verify-rename primitive.
- `repository` — `.forge` discovery and `init`.
- `object_store` — content-addressed loose-object storage (blobs, trees,
  and now commits).
- `index_store` — reads/writes the single `.forge/index` file
  (mutable, not content-addressed, so a separate abstraction from
  ObjectStore per architecture.md).
- `ref_store` — mutable branch pointers under `.forge/refs/heads/` plus
  `.forge/HEAD`; the RefStore bullet from architecture.md. Ref updates
  are compare-and-swap (ADR 0003), not blind overwrites.
- `repository` also now owns `RepositoryConfig`/`load_config` (the
  first real reader of the `storage_root` config key `init` has
  written since Phase 1, plus the optional `author_name`/`author_email`
  keys Phase 6 added) and writes the initial `.forge/HEAD` pointing at
  an unborn default branch.

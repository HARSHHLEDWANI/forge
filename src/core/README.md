# Core

Foundations used across Forge: version info, the internal
error-exception hierarchy, the explicit (non-global) logging facility,
the object-identity primitives (`hashing`, `object_id`,
`object_encoding`) that `blob` and `tree` build on, the Git-domain
object types themselves (`blob`, `tree`, `tree_builder`), and the
staging layer: `index` (the flat staged-entries domain type),
`ignore_rules` (`.forgeignore` parsing/matching), and `staging`
(`stage_path`, the `add`/`add .` orchestration — walks the working
tree, hashes and stores blobs, updates the index, stages deletions).
`hashing` is the one place with an external dependency (OpenSSL) —
cryptographic primitives use an audited library, never a bespoke one.


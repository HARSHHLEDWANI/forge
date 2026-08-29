# Core

Foundations used across Forge: version info, the internal
error-exception hierarchy, the explicit (non-global) logging facility,
and the object-identity primitives (`hashing`, `object_id`,
`object_encoding`) that `blob` (and Tree/Commit, from Phase 4 on) build
on. `hashing` is the one place with an external dependency (OpenSSL) —
cryptographic primitives use an audited library, never a bespoke one.


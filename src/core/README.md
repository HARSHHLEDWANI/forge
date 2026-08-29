# Core

Foundations used across Forge: version info, the internal
error-exception hierarchy, the explicit (non-global) logging facility,
and the object-identity primitives (`hashing`, `object_id`,
`object_encoding`) that Blob/Tree/Commit build on starting Phase 3.
`hashing` is the one place with an external dependency (OpenSSL) —
cryptographic primitives use an audited library, never a bespoke one.


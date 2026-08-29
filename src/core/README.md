# Core

Foundations used across Forge: version info, the internal
error-exception hierarchy, the explicit (non-global) logging facility,
the object-identity primitives (`hashing`, `object_id`,
`object_encoding`) that `blob` and `tree` build on, and the Git-domain
object types themselves: `blob`, `tree` (entries, canonical ordering,
encode/decode), and `tree_builder` (recursive directory -> Tree/Blob
walk). `hashing` is the one place with an external dependency
(OpenSSL) — cryptographic primitives use an audited library, never a
bespoke one.


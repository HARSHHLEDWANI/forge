# Storage Design

Object path:
`objects/<first-two-hash-chars>/<remaining-hash-chars>`

Object lifecycle:
canonical bytes → SHA-256 → temporary file → write → verify → atomic publish.

Never overwrite immutable objects, trust arbitrary paths, or load huge files entirely into RAM.

Future: compression, pack files, GC, object indexes, backup targets.

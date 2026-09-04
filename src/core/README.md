# Core

Foundations used across Forge: version info, the internal
error-exception hierarchy, the explicit (non-global) logging facility,
the object-identity primitives (`hashing`, `object_id`,
`object_encoding`) that `blob`, `tree`, and `commit` build on, the
Git-domain object types themselves (`blob`, `tree`, `tree_builder`,
`commit`), and the staging layer: `index` (the flat staged-entries
domain type), `ignore_rules` (`.forgeignore` parsing/matching), and
`staging` (`stage_path`, the `add`/`add .` orchestration — walks the
working tree, hashes and stores blobs, updates the index, stages
deletions). `hashing` is the one place with an external dependency
(OpenSSL) — cryptographic primitives use an audited library, never a
bespoke one.

Commit/ref layer (Phase 6): `committing` (`create_commit` — snapshots
the index into a Tree via `tree_builder`'s `build_tree_from_index`,
wraps it in a `Commit`, and advances the current branch), `log`
(`commit_log`, first-parent history traversal), and `branching`
(`list_branches`/`create_branch`). All three sit on top of
`storage::RefStore` for HEAD/branch state, matching the
CLI -> Application Services -> Forge Core -> Storage Abstractions
boundary in AGENTS.md.

Checkout layer (Phase 7): `checkout` (`resolve_checkout_target` —
branch name or raw commit hex; `checkout` — materializes a target
commit's tree into the working directory, index, and HEAD). Uses
`tree_builder`'s `flatten_tree_to_index` (the inverse of
`build_tree_from_index`) to know exactly which paths a target implies
without touching disk. Safety is per-path, matching real Git: a path
only blocks the switch if it's actually part of what's changing
between the old and new tree *and* the working tree has diverged from
what's recorded for it — an unrelated dirty file rides along
unaffected. The one deliberate simplification is coarser than Git:
any staged-but-uncommitted change anywhere blocks the whole checkout,
rather than being selectively carried forward, since there's no
diff/merge machinery yet (Phase 8/9) to resolve that partially.


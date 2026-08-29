# Forge --- C++ Git/GitHub System Design Grilling

## Project definition

**Forge** is a private, self-hosted developer platform built from
scratch in **C++**.

The target evolution is:

``` text
V1: Local Git engine + CLI + local HDD/SSD storage
V2: Local Forge server + multi-user LAN support
V3: GitHub-like web UI + repositories + issues + pull requests + reviews
V4: Backups + CI/CD + observability + optional multi-node scaling
```

Core principle:

> **Committed Git objects are immutable; references move. Correctness
> and data integrity come before performance.**

This document is both a design plan and a grilling/interview question
bank.

For every question: - **Recommended choice** is the option selected for
the project. - You should first try to answer without looking at the
recommendation. - Where an option is marked **Combination**, implement
the simpler subset first and evolve toward the full design. - All
implementation examples and architecture are intended for **C++**.

------------------------------------------------------------------------

# Round 1 --- Product and Requirements

## Q1. What are you building?

-   A. Git clone only
-   B. GitHub clone only
-   **C. Git engine + GitHub-like platform**
-   D. Generic file hosting

**Recommended: C.**

------------------------------------------------------------------------

## Q2. What is the main goal?

-   A. Learning only
-   B. Portfolio only
-   C. System-design interview practice only
-   **D. Learning + portfolio + system design**

**Recommended: D.**

------------------------------------------------------------------------

## Q3. Who uses the first version?

-   **A. One user on one computer**
-   B. Multiple LAN users
-   C. Public internet users
-   D. Start local, then LAN/server, then optionally public

**Recommended: A initially, evolving toward D.**

------------------------------------------------------------------------

## Q4. What is the product philosophy?

-   A. GitHub but offline
-   B. Git made easier
-   C. Developer operating system
-   **D. Private/self-hosted GitHub + developer platform**
-   E. A combination of A + B + D

**Recommended: D initially; evolve toward E.**

------------------------------------------------------------------------

## Q5. Should internet access be required?

-   **A. No**
-   B. Only for optional features
-   C. Yes
-   D. Local-first with optional internet connectivity

**Recommended: D.**

Forge must function completely offline in local mode.

------------------------------------------------------------------------

## Q6. Where should repository data live?

-   A. Cloud storage
-   B. Database only
-   **C. Local filesystem on a configurable drive**
-   D. RAM

**Recommended: C.**

Example:

``` text
D:\Forge\
    repositories\
    backups\
    logs\
    config\
```

------------------------------------------------------------------------

## Q7. What should happen if a user wants to move storage?

-   A. Edit files manually
-   B. Reinstall Forge
-   C. Configuration only
-   **D. Support configuration + migration command + installation
    setup**

**Recommended: D.**

Example:

``` bash
forge storage move E:\Forge
```

------------------------------------------------------------------------

## Q8. What should the primary interface be?

-   A. Web UI
-   **B. CLI**
-   C. GUI desktop application
-   D. API

**Recommended: B.**

The CLI is the first interface and the web UI comes later.

------------------------------------------------------------------------

## Q9. What CLI style should Forge support?

-   A. Interactive only
-   B. Traditional commands only
-   **C. Both interactive and traditional commands**
-   D. Natural language only

**Recommended: C.**

Examples:

``` bash
forge
forge status
forge commit -m "message"
```

------------------------------------------------------------------------

## Q10. What should the CLI optimize for?

-   A. Maximum command density
-   B. Git compatibility only
-   **C. Discoverability + power**
-   D. Minimum code

**Recommended: C.**

------------------------------------------------------------------------

# Round 2 --- High-Level Architecture

## Q11. What language?

-   A. Python
-   B. Java
-   C. Go
-   **D. C++**
-   E. Rust
-   F. Multiple languages

**Recommended: D.**

Reason: the project intentionally emphasizes systems programming,
filesystem operations, memory, storage, concurrency, and networking.

------------------------------------------------------------------------

## Q12. Should Forge call the existing `git` executable?

-   A. Always
-   **B. Never for the core engine**
-   C. Use Git temporarily
-   D. Shell out to Git for difficult operations

**Recommended: B.**

Forge should implement its own core Git-like engine.

------------------------------------------------------------------------

## Q13. Should Forge eventually be Git-compatible?

-   A. Never
-   **B. Yes, after the core engine works**
-   C. Compatibility is the first priority
-   D. Only the web UI needs compatibility

**Recommended: B.**

------------------------------------------------------------------------

## Q14. What should the architecture look like?

-   A. One giant application
-   B. CLI directly manipulates files
-   **C. CLI → Commands → Services → Engine → Storage**
-   D. Web UI directly manipulates storage

**Recommended: C.**

------------------------------------------------------------------------

## Q15. Should local and server modes share the same core?

-   A. Separate implementations
-   B. Server-only engine
-   **C. Same core engine with different transport layers**
-   D. Rewrite the engine for server mode

**Recommended: C.**

Conceptually:

``` text
Local:
CLI → Services → Forge Engine → Storage

Server:
CLI → Transport → Server → Services → Forge Engine → Storage
```

------------------------------------------------------------------------

## Q16. Database?

-   A. JSON
-   B. SQLite only
-   **C. PostgreSQL**
-   D. MongoDB
-   E. No database

**Recommended: C for the server-capable architecture.**

A small local-only prototype can use SQLite, but the target architecture
uses PostgreSQL for metadata.

------------------------------------------------------------------------

## Q17. What belongs in the database?

-   A. Git blobs and trees
-   **B. Users, repositories, permissions, issues, PRs, settings, jobs
    and metadata**
-   C. Every source file
-   D. Entire repositories

**Recommended: B.**

Git object storage belongs primarily in the object store.

------------------------------------------------------------------------

## Q18. How should the CLI communicate in server mode?

-   A. Shared filesystem
-   B. HTTP only
-   **C. SSH for Git-style transport, with HTTP API for application
    operations**
-   D. Database connection directly from CLI

**Recommended: C.**

------------------------------------------------------------------------

## Q19. Should the local version require a server process?

-   **A. No**
-   B. Yes
-   C. Always run a server in the background

**Recommended: A.**

Local mode should be usable without a network server.

------------------------------------------------------------------------

## Q20. Should Forge have a web UI?

-   A. No
-   **B. Yes, after the CLI and core engine**
-   C. Build it first

**Recommended: B.**

------------------------------------------------------------------------

# Round 3 --- Git Object Model

## Q21. What should a Git object be?

-   A. A database row
-   B. A complete project snapshot
-   **C. Immutable content-addressed data**
-   D. A filename

**Recommended: C.**

------------------------------------------------------------------------

## Q22. What object types should Forge implement?

-   **A. Blob, Tree, Commit, Tag**
-   B. File, Directory, Commit
-   C. Snapshot only
-   D. Database records

**Recommended: A.**

------------------------------------------------------------------------

## Q23. What should a blob contain?

-   **A. File contents**
-   B. Filename + contents
-   C. Full path + contents
-   D. Permissions + filename + contents

**Recommended: A.**

A tree owns names and directory structure.

------------------------------------------------------------------------

## Q24. What should a tree contain?

-   **A. Directory entries pointing to blobs or other trees**
-   B. File contents
-   C. Commit metadata
-   D. User information

**Recommended: A.**

------------------------------------------------------------------------

## Q25. What should a commit contain?

-   A. Complete source snapshot
-   **B. Root tree ID + parent commit ID(s) + metadata + message**
-   C. Only changed files
-   D. Only message and timestamp

**Recommended: B.**

------------------------------------------------------------------------

## Q26. How should objects be identified?

-   A. UUID
-   B. Sequential ID
-   **C. Cryptographic hash of canonical object representation**
-   D. Filename

**Recommended: C.**

------------------------------------------------------------------------

## Q27. Hash algorithm for Forge v1?

-   A. MD5
-   B. SHA-1
-   **C. SHA-256**
-   D. Both immediately

**Recommended: C.**

Compatibility with SHA-1 can be added later.

------------------------------------------------------------------------

## Q28. Where should objects be stored?

-   A. One huge file
-   **B. Content-addressed filesystem layout**
-   C. PostgreSQL
-   D. Individual files named by filename

**Recommended: B.**

Example:

``` text
objects/
    ab/
        cdef123...
```

------------------------------------------------------------------------

## Q29. Should objects be mutable?

-   A. Yes
-   B. Repository owner decides
-   **C. No**
-   D. Only commits are immutable

**Recommended: C.**

------------------------------------------------------------------------

## Q30. What should happen when identical content appears twice?

-   A. Store two copies
-   **B. Reuse the same blob**
-   C. Deduplicate later
-   D. Store it in PostgreSQL

**Recommended: B.**

------------------------------------------------------------------------

# Round 4 --- Repository Structure and Object Serialization

## Q31. What should `forge init` create?

-   A. Empty directory only
-   B. `.git`
-   **C. `.forge` with objects, refs, HEAD, index and config**
-   D. PostgreSQL database only

**Recommended: C.**

------------------------------------------------------------------------

## Q32. Suggested structure?

``` text
.forge/
├── objects/
├── refs/
│   ├── heads/
│   ├── remotes/
│   └── tags/
├── HEAD
├── index
├── config
└── logs/
```

**Recommended: this structure.**

------------------------------------------------------------------------

## Q33. How should object serialization work?

-   A. C++ object memory dump
-   B. JSON
-   **C. Canonical deterministic binary/text representation**
-   D. Random serialization

**Recommended: C.**

Never hash raw C++ memory because padding, ABI and platform differences
can change it.

------------------------------------------------------------------------

## Q34. Should object IDs depend on filenames?

-   A. Yes
-   **B. No for blobs**
-   C. Always
-   D. Only on Windows

**Recommended: B.**

Blob identity depends on content; tree entries contain names.

------------------------------------------------------------------------

## Q35. Should tree entries contain file mode/type?

-   **A. Yes**
-   B. No
-   C. Only for executables

**Recommended: A.**

This allows regular files, directories and symlinks to be represented
correctly.

------------------------------------------------------------------------

## Q36. Should Forge use a canonical byte encoding?

-   **A. Yes**
-   B. No
-   C. Platform-dependent encoding

**Recommended: A.**

This is required for deterministic hashing.

------------------------------------------------------------------------

## Q37. What should object integrity rely on?

-   A. Filename
-   B. Timestamp
-   **C. Recomputing and verifying the hash**
-   D. Database ID

**Recommended: C.**

------------------------------------------------------------------------

## Q38. What happens when an object is missing?

-   A. Silently recreate it
-   B. Ignore it
-   **C. Detect repository corruption and attempt recovery**
-   D. Generate an empty object

**Recommended: C.**

------------------------------------------------------------------------

## Q39. What happens if an object hash does not match its contents?

-   A. Trust the object
-   B. Rehash and replace it
-   **C. Report corruption and attempt recovery from backup**
-   D. Ignore the mismatch

**Recommended: C.**

------------------------------------------------------------------------

## Q40. Should object storage be platform-independent?

-   **A. Yes**
-   B. Windows-only
-   C. Linux-only

**Recommended: A.**

Use C++17/20 filesystem abstractions and avoid hardcoding
platform-specific behavior into the engine.

------------------------------------------------------------------------

# Round 5 --- Working Tree, Index and `forge add`

## Q41. What are the three important states?

-   **A. Working tree, index/staging area, repository**
-   B. Working tree, database, server
-   C. Files, folders, database

**Recommended: A.**

------------------------------------------------------------------------

## Q42. Why have an index?

-   A. For UI
-   **B. To represent what will go into the next commit**
-   C. To replace commits
-   D. To store all history

**Recommended: B.**

------------------------------------------------------------------------

## Q43. What does `forge add file.cpp` do?

-   A. Commit immediately
-   **B. Hash/store content and update the index**
-   C. Upload to server
-   D. Modify the working file

**Recommended: B.**

------------------------------------------------------------------------

## Q44. What does `forge add .` do?

-   A. Commit everything
-   **B. Scan eligible working-tree files and update the index**
-   C. Delete untracked files
-   D. Push everything

**Recommended: B.**

------------------------------------------------------------------------

## Q45. Should `forge add` load every file fully into RAM?

-   A. Yes
-   **B. No, stream files in chunks**
-   C. Only files over 1 GB

**Recommended: B.**

------------------------------------------------------------------------

## Q46. How should huge files be hashed?

-   A. Load the whole file into memory
-   **B. Stream chunks through the hash**
-   C. Reject them
-   D. Convert them to text

**Recommended: B.**

------------------------------------------------------------------------

## Q47. What should happen to deleted files?

-   A. Delete their old blobs
-   B. Immediately modify old commits
-   **C. Remove their index/tree entry for the next snapshot while
    preserving historical objects**
-   D. Ignore deletion

**Recommended: C.**

------------------------------------------------------------------------

## Q48. Should old blobs be deleted immediately after a file disappears?

-   A. Yes
-   **B. No**
-   C. Only on Windows

**Recommended: B.**

Historical commits may still reference them.

------------------------------------------------------------------------

## Q49. Should Forge support an ignore file?

-   A. No
-   **B. Yes, `.forgeignore`**
-   C. Only use `.gitignore`

**Recommended: B initially. Git-compatible ignore syntax can be added
later.**

------------------------------------------------------------------------

## Q50. What should happen to `.forge/` during `forge add .`?

-   A. Add it
-   **B. Automatically exclude it**
-   C. Delete it
-   D. Upload it

**Recommended: B.**

------------------------------------------------------------------------

# Round 6 --- Filesystem Edge Cases

## Q51. How should symbolic links be handled?

-   A. Follow them recursively
-   **B. Store the link target as a symlink object/entry**
-   C. Ignore all symlinks
-   D. Copy the target file

**Recommended: B.**

Following arbitrary symlinks can escape the repository.

------------------------------------------------------------------------

## Q52. What about file permissions?

-   A. Ignore them
-   **B. Preserve relevant mode information**
-   C. Store them in PostgreSQL
-   D. Store Windows ACLs in every blob

**Recommended: B.**

Keep the core cross-platform and model only the permissions needed by
the repository format.

------------------------------------------------------------------------

## Q53. How should binary files be detected?

-   A. Reject them
-   B. Convert them to text
-   **C. Treat file content as opaque bytes; optional heuristics only
    for UI/diff**
-   D. Store them differently as a requirement

**Recommended: C.**

------------------------------------------------------------------------

## Q54. Should Forge generate text diffs for binary files?

-   A. Yes
-   **B. No; show binary changed**
-   C. Convert to Base64

**Recommended: B.**

------------------------------------------------------------------------

## Q55. How should renames initially work?

-   A. Special rename object required
-   **B. Represent as delete + add; detect rename heuristically for UI**
-   C. Modify the blob ID
-   D. Copy the entire repository

**Recommended: B.**

------------------------------------------------------------------------

## Q56. Should file scanning be incremental?

-   A. No, scan everything every time
-   **B. Yes, use metadata/index information to avoid unnecessary work**
-   C. Only for binary files

**Recommended: B.**

------------------------------------------------------------------------

## Q57. What if a file changes while `forge add` is reading it?

-   A. Silently accept inconsistent content
-   **B. Detect unstable metadata/content and retry or report the
    change**
-   C. Lock the entire filesystem
-   D. Delete the file

**Recommended: B.**

------------------------------------------------------------------------

## Q58. Should `forge add` follow files outside the repository?

-   A. Yes
-   **B. No**
-   C. Ask for permission after following them

**Recommended: B.**

------------------------------------------------------------------------

## Q59. How should paths be stored?

-   A. Absolute paths
-   **B. Repository-relative normalized paths**
-   C. Windows drive paths
-   D. User-specific paths

**Recommended: B.**

------------------------------------------------------------------------

## Q60. What should happen if staging fails halfway through?

-   A. Leave a silently corrupted index
-   **B. Use a temporary index and atomically replace the old index
    after success**
-   C. Delete the repository
-   D. Commit partially

**Recommended: B.**

------------------------------------------------------------------------

# Round 7 --- Commits and History

## Q61. What should `forge commit` do?

-   A. Re-scan and ignore the index
-   **B. Build a tree from the index and create an immutable commit**
-   C. Modify the previous commit
-   D. Copy the repository

**Recommended: B.**

------------------------------------------------------------------------

## Q62. What should a commit point to?

-   **A. A root tree**
-   B. Every file directly
-   C. Database rows only
-   D. A ZIP

**Recommended: A.**

------------------------------------------------------------------------

## Q63. What is the parent of the first commit?

-   A. Null object
-   **B. No parent**
-   C. Repository ID
-   D. Root tree

**Recommended: B.**

------------------------------------------------------------------------

## Q64. How should history be traversed?

-   A. Separate history file
-   **B. Follow parent references**
-   C. Scan timestamps
-   D. Query filenames

**Recommended: B.**

------------------------------------------------------------------------

## Q65. How should commit IDs be generated?

-   A. Random UUID
-   **B. Hash canonical commit representation**
-   C. Sequential integer
-   D. Timestamp

**Recommended: B.**

------------------------------------------------------------------------

## Q66. Should timestamps alone identify commits?

-   A. Yes
-   **B. No**
-   C. Only locally

**Recommended: B.**

------------------------------------------------------------------------

## Q67. Should authorship be stored?

-   **A. Yes**
-   B. No
-   C. Only in the database

**Recommended: A.**

------------------------------------------------------------------------

## Q68. Should commit messages be mutable?

-   A. Yes
-   **B. No**
-   C. Only by repository owner

**Recommended: B.**

------------------------------------------------------------------------

## Q69. Should history rewriting be supported?

-   A. Never
-   **B. Yes, through explicit advanced operations, but protect shared
    branches**
-   C. Always automatically
-   D. Only by editing files

**Recommended: B.**

------------------------------------------------------------------------

## Q70. How should `forge log` initially work?

-   A. Query PostgreSQL
-   **B. Traverse the commit graph from the current reference**
-   C. Read a log file
-   D. Scan all objects

**Recommended: B.**

------------------------------------------------------------------------

# Round 8 --- Branches, HEAD and Checkout

## Q71. What is a branch?

-   A. Copy of repository
-   B. Special commit
-   **C. Mutable reference pointing to a commit**
-   D. Directory

**Recommended: C.**

------------------------------------------------------------------------

## Q72. What is HEAD?

-   A. Newest commit
-   **B. Current checkout reference/location**
-   C. Root tree
-   D. Repository owner

**Recommended: B.**

------------------------------------------------------------------------

## Q73. Typical HEAD representation?

-   **A. HEAD → refs/heads/main → commit ID**
-   B. HEAD → file
-   C. HEAD → database user
-   D. HEAD → blob

**Recommended: A.**

------------------------------------------------------------------------

## Q74. What should happen when creating a branch?

-   A. Copy every file
-   **B. Create a new reference at the current commit**
-   C. Copy all objects
-   D. Create a database snapshot

**Recommended: B.**

------------------------------------------------------------------------

## Q75. What happens when a branch receives a commit?

-   A. Old commit changes
-   **B. Branch reference moves to the new commit**
-   C. All branches move
-   D. Objects are overwritten

**Recommended: B.**

------------------------------------------------------------------------

## Q76. Should old commits remain?

-   **A. Yes**
-   B. No
-   C. Only if backed up

**Recommended: A.**

------------------------------------------------------------------------

## Q77. Should Forge support detached HEAD?

-   A. No
-   **B. Yes**
-   C. Only in server mode

**Recommended: B.**

------------------------------------------------------------------------

## Q78. What should checkout do?

-   A. Change only HEAD
-   **B. Resolve the target tree and safely update the working
    tree/index/HEAD**
-   C. Copy the repository
-   D. Delete untracked files automatically

**Recommended: B.**

------------------------------------------------------------------------

## Q79. What if checkout would overwrite uncommitted changes?

-   A. Overwrite them
-   **B. Refuse and explain which files would be lost**
-   C. Delete them
-   D. Commit them automatically

**Recommended: B.**

------------------------------------------------------------------------

## Q80. Should `forge switch` exist separately?

-   **A. Yes, as a user-friendly branch operation**
-   B. No
-   C. Only in the web UI

**Recommended: A.**

------------------------------------------------------------------------

# Round 9 --- Diff and Merge

## Q81. How should `forge diff` work?

-   A. Compare filenames only
-   **B. Compare tree/index/working-tree states and produce file-level
    diffs**
-   C. Compare timestamps
-   D. Compare database rows

**Recommended: B.**

------------------------------------------------------------------------

## Q82. What is a fast-forward merge?

-   A. Two-way conflict resolution
-   **B. Moving the target reference to a descendant commit when no
    merge commit is needed**
-   C. Copying branches
-   D. Rewriting both branches

**Recommended: B.**

------------------------------------------------------------------------

## Q83. What is a merge commit?

-   A. Commit with no parents
-   B. Commit containing two file copies
-   **C. Commit with two or more parent commits**
-   D. Special blob

**Recommended: C.**

------------------------------------------------------------------------

## Q84. How many parents does a normal merge commit have?

-   A. Zero
-   B. One
-   **C. Two**
-   D. One per file

**Recommended: C.**

------------------------------------------------------------------------

## Q85. How should a three-way merge find the base?

-   A. Newest commit
-   **B. Common ancestor/merge base**
-   C. First commit
-   D. Random commit

**Recommended: B.**

------------------------------------------------------------------------

## Q86. What should happen when both sides modify the same incompatible region?

-   A. Pick local automatically
-   B. Pick remote automatically
-   **C. Mark a conflict and require resolution**
-   D. Delete both

**Recommended: C.**

------------------------------------------------------------------------

## Q87. Should Forge automatically overwrite one side during conflicts?

-   A. Yes
-   **B. No**
-   C. Only for admins

**Recommended: B.**

------------------------------------------------------------------------

## Q88. What should conflict resolution look like?

-   A. Delete the file
-   **B. Provide conflict markers/structured conflict information and
    let the user resolve**
-   C. Pick newest timestamp
-   D. Ask the server to choose

**Recommended: B.**

------------------------------------------------------------------------

## Q89. Should merge be automatic when there is no conflict?

-   **A. Yes**
-   B. No
-   C. Never

**Recommended: A.**

------------------------------------------------------------------------

## Q90. Should `forge sync` hide merge/rebase decisions?

-   A. Always silently choose
-   **B. Offer safe interactive choices when divergence occurs**
-   C. Always rebase
-   D. Always merge

**Recommended: B.**

------------------------------------------------------------------------

# Round 10 --- Networking, Clone, Fetch and Push

## Q91. What should `forge clone` do?

-   A. Copy only the working tree
-   **B. Transfer repository metadata/objects and create a working
    checkout**
-   C. Download only the latest file versions
-   D. Export a ZIP

**Recommended: B.**

------------------------------------------------------------------------

## Q92. What should fetch do?

-   A. Modify working files
-   **B. Obtain remote objects/references without automatically changing
    the current working branch**
-   C. Delete local branches
-   D. Commit changes

**Recommended: B.**

------------------------------------------------------------------------

## Q93. What should push do?

-   A. Upload every object every time
-   **B. Negotiate/transfer missing objects and update allowed remote
    references**
-   C. Replace the entire repository
-   D. Upload only changed filenames

**Recommended: B.**

------------------------------------------------------------------------

## Q94. Should Forge transfer already-existing objects?

-   A. Always
-   **B. No, avoid redundant transfers**
-   C. Only on LAN

**Recommended: B.**

------------------------------------------------------------------------

## Q95. How should push authorization work?

-   A. Filename-based
-   **B. Authenticate user, authorize repository/ref operation**
-   C. Trust every local connection
-   D. Use only IP address

**Recommended: B.**

------------------------------------------------------------------------

## Q96. What should happen to a non-fast-forward push?

-   A. Overwrite remote
-   **B. Reject by default**
-   C. Automatically delete remote commits
-   D. Randomly merge

**Recommended: B.**

------------------------------------------------------------------------

## Q97. What does normal Git do when another user has advanced the remote branch?

-   A. Overwrite it
-   **B. Reject the non-fast-forward update**
-   C. Delete local work
-   D. Automatically rebase

**Recommended: B.**

------------------------------------------------------------------------

## Q98. How should Forge initially handle concurrent pushes?

-   A. Distributed consensus
-   B. Last writer wins
-   **C. Optimistic concurrency / compare-and-swap reference update**
-   D. Ignore concurrency

**Recommended: C.**

------------------------------------------------------------------------

## Q99. What should the server compare before updating a branch?

-   A. Username
-   **B. Expected old branch/reference value**
-   C. Timestamp only
-   D. Repository size

**Recommended: B.**

------------------------------------------------------------------------

## Q100. When should distributed consensus be considered?

-   A. Immediately
-   **B. Only if Forge becomes a multi-node replicated system requiring
    consensus**
-   C. For every local commit
-   D. Never

**Recommended: B.**

------------------------------------------------------------------------

# Round 11 --- Authentication and Authorization

## Q101. Authentication answers:

-   A. What can you access?
-   **B. Who are you?**
-   C. What branch is current?
-   D. What object is corrupt?

**Recommended: B.**

------------------------------------------------------------------------

## Q102. Authorization answers:

-   A. Who are you?
-   **B. What are you allowed to do?**
-   C. What is your password?
-   D. What is the commit hash?

**Recommended: B.**

------------------------------------------------------------------------

## Q103. What authentication methods should Forge support?

-   A. Password only
-   B. IP address
-   **C. Password/session for web + SSH keys/tokens for developer
    access**
-   D. Repository name

**Recommended: C.**

------------------------------------------------------------------------

## Q104. How should passwords be stored?

-   A. Plain text
-   B. Reversible encryption
-   **C. Strong password hashing with a modern password KDF**
-   D. SHA-256(password)

**Recommended: C.**

------------------------------------------------------------------------

## Q105. Should SSH private keys be stored on the server?

-   A. Yes
-   **B. No, only public keys should be registered**
-   C. Encrypt and store them
-   D. Store them in PostgreSQL

**Recommended: B.**

------------------------------------------------------------------------

## Q106. How should repository permissions work?

-   A. Everyone can write
-   **B. Role-based permissions**
-   C. Filename matching
-   D. IP-based permissions

**Recommended: B.**

Example:

``` text
Owner
Admin
Write
Read
```

------------------------------------------------------------------------

## Q107. Should private repositories be inaccessible without authorization?

-   **A. Yes**
-   B. No
-   C. Only through the UI

**Recommended: A.**

------------------------------------------------------------------------

## Q108. Should every important authorization action be logged?

-   **A. Yes**
-   B. No
-   C. Only failed logins

**Recommended: A.**

------------------------------------------------------------------------

## Q109. What should happen if an SSH key is revoked?

-   A. Existing sessions always remain valid forever
-   **B. Future authentication using that key must fail**
-   C. Delete the repository
-   D. Disable the entire account

**Recommended: B.**

------------------------------------------------------------------------

## Q110. Should administrative operations require stronger authorization?

-   **A. Yes**
-   B. No
-   C. Only for public servers

**Recommended: A.**

------------------------------------------------------------------------

# Round 12 --- Database Design

## Q111. What should the database primarily store?

-   A. Git blobs
-   **B. Application metadata**
-   C. Every file as a BLOB
-   D. CLI history

**Recommended: B.**

------------------------------------------------------------------------

## Q112. Main entities?

-   **A. Users, repositories, memberships, branches, issues, PRs,
    reviews, comments, organizations, jobs**
-   B. Files only
-   C. Commits only
-   D. Sessions only

**Recommended: A.**

------------------------------------------------------------------------

## Q113. Should repository IDs be stable?

-   **A. Yes**
-   B. No
-   C. Use only repository names

**Recommended: A.**

Use an internal UUID while allowing human-readable names to change.

------------------------------------------------------------------------

## Q114. What should happen if a repository is renamed?

-   A. Change every historical Git object
-   **B. Change repository metadata/name while stable repository ID
    remains**
-   C. Copy repository
-   D. Recreate all commits

**Recommended: B.**

------------------------------------------------------------------------

## Q115. What happens if a user is removed from a repository?

-   A. Delete the repository
-   **B. Remove/update their membership according to role rules**
-   C. Delete their commits
-   D. Rewrite history

**Recommended: B.**

------------------------------------------------------------------------

## Q116. Should Git object references be stored in relational tables?

-   A. Every blob must be a row
-   **B. Store important metadata/references where useful, but keep
    object content in the object store**
-   C. Never store any reference
-   D. Store all objects in PostgreSQL

**Recommended: B.**

------------------------------------------------------------------------

## Q117. Should database writes and filesystem writes always be one physical transaction?

-   A. Yes, PostgreSQL can magically transact the filesystem
-   **B. No; design explicit coordination, idempotency and recovery**
-   C. Ignore consistency
-   D. Use database only

**Recommended: B.**

------------------------------------------------------------------------

## Q118. What should happen if DB metadata succeeds but an object-store operation fails?

-   A. Ignore it
-   **B. Use transactional workflow/state, retry/recover, and make
    operations idempotent**
-   C. Delete all repositories
-   D. Restart the computer

**Recommended: B.**

------------------------------------------------------------------------

## Q119. What database should the first serious server architecture use?

-   A. JSON
-   B. SQLite forever
-   **C. PostgreSQL**
-   D. MongoDB

**Recommended: C.**

------------------------------------------------------------------------

## Q120. Should database schema migrations be versioned?

-   **A. Yes**
-   B. No
-   C. Only after production

**Recommended: A.**

------------------------------------------------------------------------

# Round 13 --- Storage Engineering

## Q121. Should objects be compressed?

-   A. Never
-   **B. Yes, eventually**
-   C. Only source code
-   D. Only metadata

**Recommended: B.**

Start simple, then introduce compression/pack storage.

------------------------------------------------------------------------

## Q122. Should Forge initially implement pack files?

-   A. Before basic Git operations
-   **B. No, implement loose objects first, then pack files**
-   C. Never
-   D. Only for commits

**Recommended: B.**

------------------------------------------------------------------------

## Q123. What are pack files for?

-   A. User authentication
-   **B. More compact object storage and efficient transfer**
-   C. Database backup only
-   D. CLI autocomplete

**Recommended: B.**

------------------------------------------------------------------------

## Q124. Should object garbage collection run during a commit?

-   A. Yes
-   **B. No, run as a background/maintenance task**
-   C. Delete everything unreferenced immediately
-   D. Only after push

**Recommended: B.**

------------------------------------------------------------------------

## Q125. When is an object potentially unreachable?

-   A. Whenever it is old
-   **B. When no reachable reference/commit graph points to it**
-   C. When its filename is long
-   D. When a user deletes a file

**Recommended: B.**

------------------------------------------------------------------------

## Q126. Should unreachable objects be deleted immediately?

-   A. Yes
-   **B. No, use a grace period before garbage collection**
-   C. Never
-   D. Only when the HDD is full

**Recommended: B.**

------------------------------------------------------------------------

## Q127. Why use a grace period?

-   A. Better UI
-   **B. Allows recovery from recently abandoned/re-written references**
-   C. Faster hashing
-   D. Authentication

**Recommended: B.**

------------------------------------------------------------------------

## Q128. How should a 15 GB file be processed?

-   A. Load into RAM
-   **B. Stream chunks**
-   C. Reject
-   D. Convert to ZIP first

**Recommended: B.**

------------------------------------------------------------------------

## Q129. Should there be a maximum file size?

-   A. No limits ever
-   **B. Configurable server/repository limits**
-   C. 1 MB
-   D. 10 MB

**Recommended: B.**

------------------------------------------------------------------------

## Q130. Should storage use atomic file replacement where possible?

-   **A. Yes**
-   B. No
-   C. Only for configuration

**Recommended: A.**

------------------------------------------------------------------------

# Round 14 --- Failure Recovery and Durability

## Q131. Power fails during a commit. What is the priority?

-   A. Speed
-   **B. Never leave silently corrupted committed state**
-   C. UI consistency
-   D. Minimal logs

**Recommended: B.**

------------------------------------------------------------------------

## Q132. How should index updates be made safe?

-   A. Write directly
-   **B. Write temporary file, flush as appropriate, atomically
    replace**
-   C. Store in RAM only
-   D. Modify previous index in place

**Recommended: B.**

------------------------------------------------------------------------

## Q133. How should a new object be written?

-   A. Write directly to final path
-   **B. Write temporary object, verify, then atomically publish**
-   C. Store in RAM
-   D. Write through PostgreSQL

**Recommended: B.**

------------------------------------------------------------------------

## Q134. Should committed objects ever be overwritten?

-   A. Yes
-   **B. No**
-   C. Only by administrators

**Recommended: B.**

------------------------------------------------------------------------

## Q135. What should happen after a crash?

-   A. Assume everything succeeded
-   **B. On restart, detect incomplete operations and recover/clean them
    safely**
-   C. Delete the repository
-   D. Rebuild from the working tree

**Recommended: B.**

------------------------------------------------------------------------

## Q136. Should Forge have a journal for complex operations?

-   A. Never
-   **B. Yes, for operations where crash recovery needs explicit state**
-   C. Only for UI
-   D. Only for logs

**Recommended: B.**

------------------------------------------------------------------------

## Q137. What should happen if the backup drive disappears?

-   A. Fail every commit
-   **B. Allow local commit, mark backup pending/failed, retry according
    to policy**
-   C. Delete local data
-   D. Shut down Forge

**Recommended: B.**

------------------------------------------------------------------------

## Q138. Should backup happen synchronously by default?

-   A. Yes, every commit waits
-   **B. No, commit locally then queue background backup**
-   C. Only for small repositories
-   D. Never

**Recommended: B.**

------------------------------------------------------------------------

## Q139. Should backup strategy be configurable?

-   **A. Yes**
-   B. No
-   C. Only by editing source code

**Recommended: A.**

------------------------------------------------------------------------

## Q140. Should backups be verified?

-   **A. Yes**
-   B. No
-   C. Only when restoring

**Recommended: A.**

Verification should include object integrity and restoration tests.

------------------------------------------------------------------------

# Round 15 --- Server and API Design

## Q141. Should application operations have an API?

-   **A. Yes**
-   B. No
-   C. Only web pages

**Recommended: A.**

------------------------------------------------------------------------

## Q142. API style?

-   **A. REST/HTTP initially**
-   B. Raw TCP only
-   C. Database API exposed to clients
-   D. SOAP

**Recommended: A.**

------------------------------------------------------------------------

## Q143. Should the web UI directly access the database?

-   A. Yes
-   **B. No, use backend services/API**
-   C. Only for reads

**Recommended: B.**

------------------------------------------------------------------------

## Q144. Where should business rules live?

-   A. React frontend
-   B. SQL triggers only
-   **C. Backend service/domain layer**
-   D. CLI only

**Recommended: C.**

------------------------------------------------------------------------

## Q145. Should CLI and web UI share business logic?

-   **A. Yes, through shared services/domain layer**
-   B. No
-   C. Duplicate the logic

**Recommended: A.**

------------------------------------------------------------------------

## Q146. Should repositories be accessed through path traversal from API input?

-   A. Yes
-   **B. No, resolve repository IDs safely and enforce storage
    boundaries**
-   C. Only admins

**Recommended: B.**

------------------------------------------------------------------------

## Q147. How should uploads be handled?

-   A. Load entire request into RAM
-   **B. Stream with size limits and validation**
-   C. Disable large files
-   D. Store in browser

**Recommended: B.**

------------------------------------------------------------------------

## Q148. Should API requests be idempotent where practical?

-   **A. Yes**
-   B. No
-   C. Only GET requests

**Recommended: A.**

------------------------------------------------------------------------

## Q149. Should long-running tasks run inside request threads?

-   A. Yes
-   **B. No, use background jobs/workers**
-   C. Only in debug

**Recommended: B.**

------------------------------------------------------------------------

## Q150. What belongs in background jobs?

-   **A. Backups, garbage collection, indexing, CI jobs, maintenance**
-   B. Password verification
-   C. Every API call
-   D. File reads only

**Recommended: A.**

------------------------------------------------------------------------

# Round 16 --- Pull Requests, Issues and Reviews

## Q151. What is a pull request?

-   A. A copy of the repository
-   **B. A request to merge one ref/branch into another, with review
    metadata**
-   C. A commit
-   D. A database backup

**Recommended: B.**

------------------------------------------------------------------------

## Q152. What should a PR reference?

-   **A. Source branch/ref + target branch/ref + repository + metadata**
-   B. Only a text description
-   C. Entire source repository
-   D. A ZIP

**Recommended: A.**

------------------------------------------------------------------------

## Q153. Should PR diffs be recomputed every time?

-   A. Always from scratch
-   **B. Cache where safe, but derive from current referenced commits**
-   C. Store a permanently trusted diff only

**Recommended: B.**

------------------------------------------------------------------------

## Q154. What happens if the source branch changes after a PR is opened?

-   A. PR becomes invalid forever
-   **B. Recompute/update its mergeability and diff**
-   C. Delete the PR
-   D. Copy old commits into a new repository

**Recommended: B.**

------------------------------------------------------------------------

## Q155. Can a PR be merged if the target branch changed?

-   A. Always
-   **B. Only if current mergeability/branch protection rules permit**
-   C. Never
-   D. Ignore the target branch

**Recommended: B.**

------------------------------------------------------------------------

## Q156. Should reviews be stored?

-   **A. Yes**
-   B. No
-   C. Only as text files

**Recommended: A.**

------------------------------------------------------------------------

## Q157. Should comments be tied to immutable identities?

-   **A. Yes, reference stable users and PR/issues**
-   B. No
-   C. Use usernames only

**Recommended: A.**

------------------------------------------------------------------------

## Q158. Should issues and PRs be database entities?

-   **A. Yes**
-   B. Git objects
-   C. Blobs

**Recommended: A.**

------------------------------------------------------------------------

## Q159. Should labels be normalized?

-   **A. Yes, use reusable label entities/relationships**
-   B. Store one comma-separated string
-   C. Store in filenames

**Recommended: A.**

------------------------------------------------------------------------

## Q160. Should PR merge operations be atomic with branch updates?

-   **A. Yes, the merge/ref update must be protected against concurrent
    target changes**
-   B. No
-   C. Only for admins

**Recommended: A.**

------------------------------------------------------------------------

# Round 17 --- CLI Architecture and UX

## Q161. How should the CLI be structured?

-   A. Everything in `main.cpp`
-   B. One giant switch statement
-   **C. Command → Service → Engine**
-   D. CLI directly accesses PostgreSQL

**Recommended: C.**

------------------------------------------------------------------------

## Q162. What should `main()` do?

-   A. Implement Git
-   **B. Parse/bootstrap and dispatch commands**
-   C. Manipulate objects
-   D. Perform database migrations

**Recommended: B.**

------------------------------------------------------------------------

## Q163. Should commands know filesystem implementation details?

-   A. Yes
-   **B. No, services/engine should own domain behavior**
-   C. Only in production

**Recommended: B.**

------------------------------------------------------------------------

## Q164. Should CLI have autocomplete?

-   **A. Yes**
-   B. No
-   C. Only for server mode

**Recommended: A.**

------------------------------------------------------------------------

## Q165. Should `forge` without arguments open an interactive menu?

-   **A. Yes**
-   B. No
-   C. Error

**Recommended: A.**

------------------------------------------------------------------------

## Q166. Should `forge help` be comprehensive?

-   **A. Yes**
-   B. No
-   C. Only web documentation

**Recommended: A.**

------------------------------------------------------------------------

## Q167. Should dangerous commands require confirmation?

-   **A. Yes**
-   B. No
-   C. Only if repository is remote

**Recommended: A.**

------------------------------------------------------------------------

## Q168. Example dangerous operations?

-   **A. Force push, delete branch, storage migration, destructive
    cleanup**
-   B. `status`
-   C. `log`

**Recommended: A.**

------------------------------------------------------------------------

## Q169. Should CLI errors explain recovery steps?

-   **A. Yes**
-   B. No
-   C. Only print error codes

**Recommended: A.**

------------------------------------------------------------------------

## Q170. Should `forge sync` automatically make irreversible choices?

-   A. Yes
-   **B. No; safe automatic operations are fine, but ambiguous merges
    require user choice**
-   C. Always rebase
-   D. Always merge

**Recommended: B.**

------------------------------------------------------------------------

# Round 18 --- Concurrency

## Q171. Two users push simultaneously. What should happen?

-   A. Last writer wins
-   **B. Compare expected ref value and reject stale update**
-   C. Random winner
-   D. Distributed consensus immediately

**Recommended: B.**

------------------------------------------------------------------------

## Q172. What should protect a local repository from concurrent writers?

-   A. Nothing
-   **B. Repository/ref locking or atomic compare-and-swap**
-   C. UI lock
-   D. Username

**Recommended: B.**

------------------------------------------------------------------------

## Q173. Should readers be blocked by every writer?

-   A. Yes
-   **B. No; use appropriate read/write coordination**
-   C. Always
-   D. Never

**Recommended: B.**

------------------------------------------------------------------------

## Q174. What should happen if two local processes run `forge commit` simultaneously?

-   A. Corrupt the repository
-   **B. Coordinate through repository locking/transaction boundaries**
-   C. Let filesystem decide
-   D. Merge commits automatically

**Recommended: B.**

------------------------------------------------------------------------

## Q175. Should object creation be globally locked?

-   A. Yes
-   **B. No, immutable object creation can be concurrent if publication
    is safe**
-   C. Only one thread in the entire application

**Recommended: B.**

------------------------------------------------------------------------

## Q176. Should branch reference updates be atomic?

-   **A. Yes**
-   B. No
-   C. Only remotely

**Recommended: A.**

------------------------------------------------------------------------

## Q177. What concurrency model should the first server use?

-   **A. Optimistic concurrency + targeted locking**
-   B. Distributed consensus
-   C. Single global mutex
-   D. No concurrency control

**Recommended: A.**

------------------------------------------------------------------------

## Q178. When would a global lock be acceptable?

-   **A. Very early prototype**
-   B. Final architecture
-   C. Never

**Recommended: A only as a temporary simplification.**

------------------------------------------------------------------------

## Q179. What is a major danger of over-locking?

-   A. Data corruption
-   **B. Reduced concurrency and throughput**
-   C. Hash collisions
-   D. Authentication failures

**Recommended: B.**

------------------------------------------------------------------------

## Q180. What should happen when a lock holder crashes?

-   A. Repository remains permanently locked
-   **B. Use process-safe locks/leases or recovery rules so stale locks
    can be detected**
-   C. Delete the repository
-   D. Ignore locks

**Recommended: B.**

------------------------------------------------------------------------

# Round 19 --- Security

## Q181. Path traversal attack:

``` text
../../../../important-file
```

What should happen?

-   A. Allow
-   **B. Reject and enforce repository-root boundaries**
-   C. Normalize and allow anywhere
-   D. Run as administrator

**Recommended: B.**

------------------------------------------------------------------------

## Q182. Should uploaded repository content be trusted?

-   A. Yes
-   **B. No**
-   C. Only if user is admin

**Recommended: B.**

------------------------------------------------------------------------

## Q183. Should Forge execute arbitrary repository code during normal Git operations?

-   A. Yes
-   **B. No**
-   C. Only for public repositories

**Recommended: B.**

------------------------------------------------------------------------

## Q184. What should CI jobs use?

-   A. Main server process
-   **B. Isolated/sandboxed worker environment**
-   C. PostgreSQL
-   D. CLI thread

**Recommended: B.**

------------------------------------------------------------------------

## Q185. Should rate limiting exist?

-   **A. Yes for server/public-facing deployments**
-   B. No
-   C. Only login

**Recommended: A.**

------------------------------------------------------------------------

## Q186. Should secrets be committed?

-   A. Yes
-   **B. No; provide secret-management/configuration mechanisms**
-   C. Encrypt them with the repository password

**Recommended: B.**

------------------------------------------------------------------------

## Q187. Should API tokens be stored in plaintext?

-   A. Yes
-   **B. No**
-   C. Only locally

**Recommended: B.**

------------------------------------------------------------------------

## Q188. Should audit logs exist?

-   **A. Yes**
-   B. No
-   C. Only errors

**Recommended: A.**

------------------------------------------------------------------------

## Q189. Should server communication use encryption?

-   A. No
-   **B. Yes for remote/network deployments**
-   C. Only passwords

**Recommended: B.**

------------------------------------------------------------------------

## Q190. Should local-only mode require TLS?

-   A. Always
-   **B. Not necessarily; secure local IPC/LAN deployment according to
    threat model**
-   C. Never

**Recommended: B.**

------------------------------------------------------------------------

# Round 20 --- Reliability and Observability

## Q191. What should Forge log?

-   **A. Errors, important operations, authentication/security events,
    jobs and performance signals**
-   B. Every byte
-   C. Nothing
-   D. Passwords

**Recommended: A.**

------------------------------------------------------------------------

## Q192. Should passwords/tokens appear in logs?

-   A. Yes
-   **B. Never**
-   C. Only debug builds

**Recommended: B.**

------------------------------------------------------------------------

## Q193. What metrics matter?

-   **A. Request latency, errors, push/fetch rates, storage usage, job
    failures, backup status**
-   B. Only CPU
-   C. Only memory
-   D. Number of usernames

**Recommended: A.**

------------------------------------------------------------------------

## Q194. Should Forge expose health checks?

-   **A. Yes**
-   B. No
-   C. Only CLI

**Recommended: A.**

------------------------------------------------------------------------

## Q195. Should background jobs be observable?

-   **A. Yes**
-   B. No
-   C. Only CI

**Recommended: A.**

------------------------------------------------------------------------

## Q196. What happens if PostgreSQL is unavailable?

-   A. Pretend it works
-   **B. Fail metadata-dependent operations safely and surface a clear
    service-health error**
-   C. Delete repositories
-   D. Recreate the database automatically without backup

**Recommended: B.**

------------------------------------------------------------------------

## Q197. What happens if object storage is unavailable?

-   A. Return fake success
-   **B. Fail storage-dependent operations safely**
-   C. Delete database records
-   D. Use RAM forever

**Recommended: B.**

------------------------------------------------------------------------

## Q198. Should errors have stable machine-readable codes?

-   **A. Yes**
-   B. No
-   C. Only in production

**Recommended: A.**

------------------------------------------------------------------------

## Q199. Should users receive human-readable explanations too?

-   **A. Yes**
-   B. No
-   C. Only logs

**Recommended: A.**

------------------------------------------------------------------------

## Q200. What is the reliability priority?

-   A. Availability above all
-   B. Speed above all
-   **C. Data integrity first, then recoverability, then performance**
-   D. UI responsiveness only

**Recommended: C.**

------------------------------------------------------------------------

# Round 21 --- Backups and Disaster Recovery

## Q201. What backup strategy should local Forge initially use?

-   A. No backup
-   **B. Configurable local backup with background jobs**
-   C. Cloud-only
-   D. Every commit blocks until backup finishes

**Recommended: B.**

------------------------------------------------------------------------

## Q202. Should backups be full copies every time?

-   A. Yes
-   **B. No; use incremental/deduplicated approaches as the system
    matures**
-   C. Never backup objects

**Recommended: B.**

------------------------------------------------------------------------

## Q203. Should backup restoration be tested?

-   **A. Yes**
-   B. No
-   C. Only after a failure

**Recommended: A.**

------------------------------------------------------------------------

## Q204. What is a backup that has never been restored?

-   A. Perfect
-   **B. Unverified**
-   C. Corrupt
-   D. Temporary

**Recommended: B.**

------------------------------------------------------------------------

## Q205. Should backups preserve immutable object integrity?

-   **A. Yes**
-   B. No
-   C. Only metadata

**Recommended: A.**

------------------------------------------------------------------------

## Q206. What should happen after catastrophic primary-storage loss?

-   A. Rebuild from working trees
-   **B. Restore repository objects and metadata from verified backups**
-   C. Recreate commits from timestamps
-   D. Start over

**Recommended: B.**

------------------------------------------------------------------------

## Q207. Should the system have a documented recovery procedure?

-   **A. Yes**
-   B. No
-   C. Only administrators need to know

**Recommended: A.**

------------------------------------------------------------------------

## Q208. Should backup failures be retried?

-   **A. Yes, with bounded retries/backoff and visible failure state**
-   B. Forever without logging
-   C. Never

**Recommended: A.**

------------------------------------------------------------------------

## Q209. Should backup storage be independent from primary storage?

-   **A. Yes**
-   B. No
-   C. Same folder

**Recommended: A.**

------------------------------------------------------------------------

## Q210. Should users be able to inspect backup status?

-   **A. Yes**
-   B. No
-   C. Only in logs

**Recommended: A.**

------------------------------------------------------------------------

# Round 22 --- Scaling

## Q211. What should the first deployment be?

-   **A. Single machine**
-   B. Kubernetes
-   C. Distributed cluster
-   D. Cloud-only

**Recommended: A.**

------------------------------------------------------------------------

## Q212. What should scale first when the system grows?

-   A. Everything simultaneously
-   **B. Identify actual bottlenecks using metrics**
-   C. Add Kubernetes immediately
-   D. Add consensus

**Recommended: B.**

------------------------------------------------------------------------

## Q213. What is a likely storage bottleneck?

-   **A. Large object reads/writes and repository operations**
-   B. Username lookup
-   C. UI CSS
-   D. Password field

**Recommended: A.**

------------------------------------------------------------------------

## Q214. What is a likely metadata bottleneck?

-   A. Blob hashing
-   **B. Database queries for users/repos/issues/PRs**
-   C. HDD filenames
-   D. CLI rendering

**Recommended: B.**

------------------------------------------------------------------------

## Q215. Should caching be introduced immediately?

-   A. Yes, everywhere
-   **B. Only after measuring hot paths**
-   C. Never

**Recommended: B.**

------------------------------------------------------------------------

## Q216. What could Redis eventually be used for?

-   A. Permanent Git objects
-   **B. Caching, sessions, rate limits, ephemeral coordination where
    appropriate**
-   C. Source-of-truth commits
-   D. Backup

**Recommended: B.**

------------------------------------------------------------------------

## Q217. Should Git objects be stored in Redis?

-   A. Yes
-   **B. No**
-   C. Only commits

**Recommended: B.**

------------------------------------------------------------------------

## Q218. When should object storage become separate from the application server?

-   A. Immediately
-   **B. When scale/reliability requirements justify it**
-   C. Never

**Recommended: B.**

------------------------------------------------------------------------

## Q219. When should distributed consensus be introduced?

-   A. At 10 users
-   B. At 100 repositories
-   **C. Only when multiple authoritative nodes need coordinated state**
-   D. Never

**Recommended: C.**

------------------------------------------------------------------------

## Q220. What should be the scaling philosophy?

-   A. Design for infinite scale from day one
-   **B. Build a correct single-node system, then evolve based on
    measured requirements**
-   C. Use every distributed technology immediately
-   D. Avoid scaling forever

**Recommended: B.**

------------------------------------------------------------------------

# Round 23 --- CI/CD and Developer Platform

## Q221. Should Forge eventually support CI?

-   A. No
-   **B. Yes, after the repository platform is stable**
-   C. Build CI first

**Recommended: B.**

------------------------------------------------------------------------

## Q222. How should CI jobs execute?

-   A. Inside the API thread
-   **B. Background worker in an isolated environment**
-   C. Inside PostgreSQL
-   D. On the user's browser

**Recommended: B.**

------------------------------------------------------------------------

## Q223. Should CI jobs have resource limits?

-   **A. Yes**
-   B. No
-   C. Only CPU

**Recommended: A.**

------------------------------------------------------------------------

## Q224. Should CI access arbitrary host files?

-   A. Yes
-   **B. No**
-   C. Only if repository owner asks

**Recommended: B.**

------------------------------------------------------------------------

## Q225. Should CI artifacts be stored separately from Git objects?

-   **A. Yes**
-   B. No
-   C. Inside commits

**Recommended: A.**

------------------------------------------------------------------------

## Q226. Should CI results be associated with commits/PRs?

-   **A. Yes**
-   B. No
-   C. Only with repositories

**Recommended: A.**

------------------------------------------------------------------------

## Q227. Should branch protection depend on CI status?

-   **A. Eventually yes**
-   B. No
-   C. Only for private repositories

**Recommended: A.**

------------------------------------------------------------------------

## Q228. Should CI be able to deploy automatically?

-   A. Always
-   **B. Optional and explicitly configured**
-   C. Never

**Recommended: B.**

------------------------------------------------------------------------

## Q229. Should deployments use secrets directly from repository files?

-   A. Yes
-   **B. No, use controlled secret injection**
-   C. Encrypt files with a hardcoded key

**Recommended: B.**

------------------------------------------------------------------------

## Q230. Should CI architecture be built before PRs/issues?

-   A. Yes
-   **B. No, repository/PR foundations first**
-   C. At the same time

**Recommended: B.**

------------------------------------------------------------------------

# Round 24 --- Testing and Engineering Quality

## Q231. What should be tested first?

-   **A. Git object serialization, hashing, refs, index and storage**
-   B. UI colors
-   C. Deployment scripts
-   D. Load balancing

**Recommended: A.**

------------------------------------------------------------------------

## Q232. Should object serialization have deterministic tests?

-   **A. Yes**
-   B. No
-   C. Only integration tests

**Recommended: A.**

------------------------------------------------------------------------

## Q233. Should crash recovery be tested?

-   **A. Yes**
-   B. No
-   C. Only manually

**Recommended: A.**

------------------------------------------------------------------------

## Q234. Should corrupted objects be tested?

-   **A. Yes**
-   B. No
-   C. Only in production

**Recommended: A.**

------------------------------------------------------------------------

## Q235. Should concurrent pushes be tested?

-   **A. Yes**
-   B. No
-   C. Only after scaling

**Recommended: A.**

------------------------------------------------------------------------

## Q236. Should CLI tests invoke the real executable?

-   **A. Yes, alongside unit tests**
-   B. No
-   C. Only manually

**Recommended: A.**

------------------------------------------------------------------------

## Q237. Should storage tests use temporary directories?

-   **A. Yes**
-   B. No
-   C. Production HDD

**Recommended: A.**

------------------------------------------------------------------------

## Q238. Should tests run on more than one OS eventually?

-   **A. Yes**
-   B. No
-   C. Only Linux

**Recommended: A.**

------------------------------------------------------------------------

## Q239. Should fuzzing be used for parsers/object formats?

-   **A. Yes, eventually**
-   B. No
-   C. Only UI

**Recommended: A.**

------------------------------------------------------------------------

## Q240. Should performance benchmarks exist?

-   **A. Yes**
-   B. No
-   C. Only after production

**Recommended: A.**

------------------------------------------------------------------------

# Round 25 --- Final Architecture Review

## Q241. Which architecture should Forge start with?

**Recommended architecture:**

``` text
                    ┌──────────────────────┐
                    │      Forge CLI       │
                    │ Interactive + direct │
                    └──────────┬───────────┘
                               │
                     Command / Service Layer
                               │
                    ┌──────────▼───────────┐
                    │     Forge Engine     │
                    │                      │
                    │ Objects              │
                    │ Trees                │
                    │ Commits              │
                    │ Refs                 │
                    │ Index                │
                    │ Diff                 │
                    │ Merge                │
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │    Storage Layer     │
                    │                      │
                    │ Object Store         │
                    │ Ref Store            │
                    │ Index Store          │
                    └──────────┬───────────┘
                               │
                         Local HDD/SSD
```

------------------------------------------------------------------------

## Q242. What should the server architecture eventually become?

**Recommended:**

``` text
CLI / Web UI
     │
     ▼
Transport Layer
(HTTP / SSH)
     │
     ▼
API / Application Services
     │
     ├───────────────┐
     ▼               ▼
Forge Engine      PostgreSQL
     │
     ▼
Object Storage
     │
     ├── Primary
     └── Backup
```

------------------------------------------------------------------------

## Q243. What should be shared between local and server modes?

-   **A. Domain/Forge engine, object model, storage abstractions and
    services where appropriate**
-   B. Nothing
-   C. Only the UI

**Recommended: A.**

------------------------------------------------------------------------

## Q244. What should remain replaceable?

-   **A. Storage backend, transport, database adapter and CLI/UI**
-   B. Commit format
-   C. Hashes after objects are created

**Recommended: A.**

Use interfaces/abstractions where they provide real architectural value.

------------------------------------------------------------------------

## Q245. What is the most important invariant?

-   A. Fast CLI
-   B. Small binary
-   **C. Immutable objects and correct references**
-   D. Pretty web UI

**Recommended: C.**

------------------------------------------------------------------------

## Q246. What is the most important failure rule?

-   A. Hide errors
-   B. Continue at all costs
-   **C. Never report success for an operation whose durable state is
    not safely established**
-   D. Delete partial data

**Recommended: C.**

------------------------------------------------------------------------

## Q247. What should be built first?

-   **A. Object model → storage → refs → index → add → commit → branch →
    checkout**
-   B. Web UI
-   C. CI/CD
-   D. Distributed storage

**Recommended: A.**

------------------------------------------------------------------------

## Q248. What should be built second?

-   **A. Diff/merge → local CLI polish → clone/fetch/push**
-   B. Kubernetes
-   C. Organizations
-   D. CI/CD

**Recommended: A.**

------------------------------------------------------------------------

## Q249. What should be built third?

-   **A. Server/API → authentication → permissions → PostgreSQL
    metadata**
-   B. Distributed consensus
-   C. Mobile app
-   D. Cloud deployment

**Recommended: A.**

------------------------------------------------------------------------

## Q250. What should be built after the core platform?

-   **A. Web UI → issues → PRs → reviews → CI → backups → observability
    → scaling**
-   B. Rewrite everything
-   C. Add random features
-   D. Kubernetes first

**Recommended: A.**

------------------------------------------------------------------------

# Final Question --- Interview-Style Defense

## Q251. Defend your Forge architecture without looking at the recommendations.

You are now in a system-design interview.

The interviewer says:

> **"Design a private, self-hosted GitHub-like platform in C++ that
> works completely offline on one machine, stores repositories on a
> configurable HDD/SSD, provides a CLI, and can later evolve into a
> multi-user server."**

You must defend the system.

Do **not** simply repeat the architecture.

Answer these as if the interviewer is actively challenging you:

### Architecture

1.  Why C++?
2.  Why build your own Git engine instead of calling Git?
3.  Why separate CLI, services, engine and storage?
4.  Why should local and server modes share the core?

### Git internals

5.  Explain blobs, trees and commits.
6.  Why are blobs content-addressed?
7.  Why are objects immutable?
8.  Why are branches mutable?
9.  What exactly does HEAD point to?
10. How does a commit reconstruct the repository state?

### Storage

11. Why filesystem object storage instead of putting every blob in
    PostgreSQL?
12. Why SHA-256?
13. How do you detect object corruption?
14. What happens if the machine loses power during a commit?
15. What happens if the HDD dies?
16. How does backup recovery work?
17. How do you prevent partial index/object writes?

### Concurrency

18. Two users push simultaneously. What happens?
19. Why don't you use Raft immediately?
20. How do you atomically update a branch?
21. What happens if the process crashes while holding a lock?

### Networking

22. Why SSH for Git operations?
23. Why HTTP for application APIs?
24. What happens during `push`?
25. How do you avoid transferring objects the server already has?
26. What happens when a push is non-fast-forward?

### Database

27. Why PostgreSQL?
28. What belongs in PostgreSQL?
29. What belongs in object storage?
30. How do you coordinate database state with filesystem state?

### Security

31. How do you authenticate users?
32. How do SSH keys work?
33. How do you enforce repository permissions?
34. How do you prevent path traversal?
35. How do you safely run CI jobs?

### Scaling

36. What is your first bottleneck?
37. What would you cache?
38. When would you introduce Redis?
39. When would you split storage from the server?
40. When would consensus become necessary?

### Failure scenarios

41. PostgreSQL goes down during a PR merge. What happens?
42. Object storage disappears during a push. What happens?
43. Backup storage disappears. Can users still commit?
44. An object hash is wrong. What do you do?
45. A repository contains millions of objects. What changes?

### Product/CLI

46. Why have both interactive and traditional CLI modes?
47. Why should `forge sync` ask the user about ambiguous merges?
48. How do you make the CLI discoverable without requiring command
    memorization?
49. How do you keep the CLI independent of the storage implementation?
50. What makes Forge meaningfully different from simply installing
    Gitea/Forgejo?

### Final interviewer challenge

The interviewer says:

> **"Your design is too complicated for a single machine. Convince me it
> isn't."**

Defend the design.

Then they say:

> **"Now your project has 100,000 repositories and 10,000 concurrent
> users. What breaks first?"**

Defend your answer.

Then:

> **"Your HDD has failed. PostgreSQL is healthy. How much data can you
> recover?"**

Defend your recovery strategy.

Then:

> **"Two users push conflicting commits to `main` at exactly the same
> time. Show me precisely what happens."**

Defend your concurrency model.

Finally:

> **"If you had six months to build Forge, what would you deliberately
> NOT build?"**

Give a prioritized answer.

------------------------------------------------------------------------

# Suggested Implementation Order

``` text
Phase 1
├── C++ project structure
├── CLI parser
├── forge init
└── repository configuration

Phase 2
├── SHA-256
├── canonical object serialization
├── blob
├── tree
├── commit
└── object store

Phase 3
├── refs
├── HEAD
├── branches
├── index
└── forge add

Phase 4
├── forge commit
├── status
├── log
├── diff
├── checkout
└── switch

Phase 5
├── merge base
├── three-way merge
├── conflict detection
└── conflict resolution

Phase 6
├── local server
├── HTTP API
├── SSH transport
├── clone
├── fetch
└── push

Phase 7
├── PostgreSQL
├── users
├── authentication
├── permissions
└── repositories

Phase 8
├── Web UI
├── issues
├── pull requests
├── reviews
└── branch protection

Phase 9
├── backups
├── background workers
├── garbage collection
├── observability
└── CI/CD

Phase 10
├── performance
├── caching
├── replication
├── multi-node architecture
└── optional consensus
```

# Core Engineering Principles

1.  **Objects are immutable; references move.**
2.  **Content determines object identity.**
3.  **Never silently lose committed data.**
4.  **Correctness before performance.**
5.  **Local-first before distributed.**
6.  **Build abstractions where they isolate real change.**
7.  **Use optimistic concurrency before distributed consensus.**
8.  **Stream large files instead of loading them into RAM.**
9.  **Treat filesystem and database consistency as an explicit
    problem.**
10. **Every backup must eventually be restorable.**
11. **Make destructive operations explicit.**
12. **Measure before optimizing.**
13. **Keep the Git engine independent from the CLI and UI.**
14. **Keep the storage backend replaceable.**
15. **Do not build distributed infrastructure until the single-node
    system is correct.**

# Interview Goal

By the end of this project, you should be able to draw the architecture
from memory and explain:

``` text
User
 ↓
CLI / Web
 ↓
Transport
 ↓
Application Services
 ↓
Forge Engine
 ├── Object Model
 ├── Index
 ├── Refs
 ├── Diff
 └── Merge
 ↓
Storage
 ├── Object Store
 ├── Metadata
 └── Backup
```

and defend every major arrow.

The ultimate goal is not merely to say:

> "I built a GitHub clone."

The goal is to be able to say:

> **"I designed and implemented a C++ local-first Git hosting platform,
> including a content-addressed version-control engine, repository
> storage, CLI, networking, authentication, concurrency control, failure
> recovery, and a path toward multi-user self-hosting."**

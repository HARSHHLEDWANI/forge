# Forge Architecture

## Local
```text
CLI / Interactive CLI
        ↓
Command + Application Services
        ↓
Forge Core
 ├── Objects
 ├── Hashing/Serialization
 ├── Index
 ├── Refs/HEAD
 ├── Diff
 └── Merge
        ↓
Storage Abstractions
 ├── ObjectStore
 ├── RefStore
 ├── IndexStore
 └── ConfigStore
        ↓
Local HDD/SSD
```

## Future server
```text
CLI / Web UI
      ↓
HTTP / SSH
      ↓
Application Services
   ↙          ↘
Forge Core   PostgreSQL
   ↓
Object Storage
   ↓
Primary + Backup
```

## Objects
Blob = bytes.
Tree = named entries pointing to objects.
Commit = root tree + parents + metadata + message.
Ref = mutable name pointing to commit.
HEAD = symbolic ref or detached commit.

## Identity
`ObjectId = SHA-256(canonical object bytes)`.
Never hash raw C++ object memory.

## Durability
Temporary file → write → verify → flush/sync as appropriate → atomic publish.

## Concurrency
Immutable objects can be created concurrently. Mutable refs use atomic compare/update semantics.

## Database boundary
PostgreSQL eventually stores users, repositories, permissions, issues, PRs, reviews, jobs and application metadata. Git object contents remain in object storage.

## Principle
Build a correct single-node system before distributed systems.

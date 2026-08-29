# Data Model

## Git domain
```text
Blob
  content: bytes

Tree
  entries[]:
    name
    mode/type
    object_id

Commit
  tree_id
  parent_ids[]
  author
  timestamp
  message

Ref
  name
  target_commit_id

HEAD
  symbolic ref OR detached commit ID
```

## Future application metadata
User, Repository, Membership, Organization, Issue, Label, PullRequest, Review, Comment, Job, Artifact, AuditEvent.

Git objects and application metadata remain separate concepts.

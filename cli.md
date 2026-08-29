# CLI Design

Direct:
```text
forge init
forge status
forge add .
forge commit -m "message"
forge log
forge branch
forge switch feature
forge diff
forge merge feature
```

`forge` opens interactive mode.

Interactive mode should help users discover status, staging, commits, branches, history, diff, merge and verification.

UX rules: clear errors, useful next-command suggestions, confirmations for destructive actions, shell completion, conventional CLI behavior, and no silent resolution of ambiguous merges.

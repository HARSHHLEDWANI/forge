# Mandatory Failure Scenarios

1. Power loss during object write.
2. Power loss during index update.
3. Power loss during ref update.
4. Corrupt object.
5. Missing object.
6. Concurrent commit.
7. Concurrent push.
8. PostgreSQL unavailable.
9. Backup unavailable.
10. Primary storage failure.

For each: define state before, state after, detection, recovery and user-visible behavior.

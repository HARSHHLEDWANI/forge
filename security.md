# Security Model

Threats: path traversal, malicious repository content, corruption, unauthorized access, credential leakage, malicious CI and DoS.

Rules:
- enforce repository-root boundaries
- separate authentication from authorization
- use a modern password KDF
- never store server-side SSH private keys
- never log secrets
- do not execute repository code during ordinary operations
- isolate CI
- use encrypted transport for remote deployments
- audit security-sensitive actions
- rate-limit public services

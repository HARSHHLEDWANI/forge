# Production deployment

Runs forge-server + forge-worker behind [Caddy](https://caddyserver.com/),
which terminates TLS and is the only service reachable from outside
this compose network — forge-server/forge-worker speak plain HTTP
internally and are never given a host port of their own (see
`docker-compose.prod.yml`'s comments). This is what
`docs/adr/0007-tls-reverse-proxy.md` decided and why.

## What you need before running this

1. **A server** (VM, VPS, bare metal — anything that can run Docker and
   is reachable on the internet at a public IP).
2. **A domain name whose A (and/or AAAA) record already points at that
   server's public IP.** Caddy requests a Let's Encrypt certificate for
   exactly the domain you give it in `.env`, over HTTP-01 challenge —
   this fails until DNS is actually resolving there, so get DNS
   propagated *before* first bring-up (`dig +short your-domain` should
   show the server's IP).
3. **Ports 80 and 443 open** to that server (security group / firewall
   rule) — 80 for the ACME challenge and the HTTP→HTTPS redirect, 443
   for the real traffic.
4. Docker and the Docker Compose plugin installed on that server.

Nothing else is required from you — rate limiting, the database
password never appearing in a process listing, and CLI credential
storage are already handled (see the project's other recent changes);
TLS itself was the one piece that needed a real domain to exist at all.

## Bring-up

```sh
git clone <this repo> forge && cd forge/deploy
cp .env.example .env
# edit .env: set FORGE_DOMAIN to your real domain, FORGE_DB_PASSWORD to
# a real random password
docker compose -f docker-compose.prod.yml up -d --build
```

First start takes a minute or two: building the `forge` image, waiting
for Postgres's health check, forge-server applying its database
migrations on startup (see `src/main_server.cpp`), then Caddy
requesting its first certificate. Watch it with:

```sh
docker compose -f docker-compose.prod.yml logs -f
```

Once it settles, `https://<your domain>/healthz` should return
`{"status":"ok"}`, and a normal browser visit shouldn't show any
certificate warning.

## Everyday operations

- **Update to a new version**: `git pull`, then
  `docker compose -f docker-compose.prod.yml up -d --build` again —
  Postgres/repo/backup data lives in named volumes (`forge-postgres-data`,
  `forge-repos`, `forge-data`, `forge-backups`), untouched by a rebuild.
- **Logs for one service**: `docker compose -f docker-compose.prod.yml logs -f forge-server` (or `forge-worker`, `caddy`, `postgres`).
- **Back up the database** (separate from `forge backup`, which backs up
  repository objects/refs — see `core/backup.hpp` — not collaboration
  data): standard `pg_dump` against the `postgres` service, or snapshot
  the `forge-postgres-data` volume.
- **Certificate renewal**: automatic — Caddy renews well before
  expiry and needs nothing from you as long as ports 80/443 stay open
  and DNS keeps resolving to this server.
- **Creating the first admin user**: register over the API, then log
  in with the CLI, exactly like any other user —
  ```sh
  curl -X POST "https://<your domain>/users?username=you" -d "<password>"
  forge login <your domain> --username you
  ```
  The first repository anyone pushes to over an authenticated
  connection makes that pusher its Admin (see `server/app.cpp`'s POST
  `/ref` bootstrap) — from there, `POST /permissions` grants roles to
  everyone else.

## What this doesn't handle

- **Horizontal scaling / multiple forge-server replicas**: not
  supported — `docs/adr/0006-scale-deferred.md` covers why this
  project stays single-node until real measurement says otherwise.
  This compose file matches that: one `forge-server`, one
  `forge-worker` (bump `--threads`/replicas only after you've actually
  measured a need).
- **Database backups being automatic**: `postgres`'s volume isn't
  snapshotted on any schedule by this compose file — wire that up
  through whatever backup tooling your host already provides.
- **Multi-domain / wildcard certificates**: the `Caddyfile` here is for
  one `FORGE_DOMAIN`; edit it directly for anything more elaborate.

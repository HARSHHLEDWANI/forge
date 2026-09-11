########################################################################
# Production image for forge-server / forge-worker.
#
# Multi-stage: the build stage reuses the same toolchain as
# Dockerfile.dev (gcc-13, cmake, ninja, libssl-dev, libpq-dev) so a
# production build never drifts from what's actually been compiled and
# tested throughout this project; the runtime stage only needs the
# *shared libraries* those link against (libssl3, libpq5), not the
# whole compiler toolchain, keeping the shipped image small and
# reducing its attack surface.
#
# Build from the repository root:
#   docker build -f deploy/Dockerfile -t forge:latest .
########################################################################

FROM gcc:13-bookworm AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake ninja-build libssl-dev libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .

# Statically links libstdc++/libgcc: the runtime stage below is
# debian:bookworm-slim, whose system libstdc++6 predates the one this
# gcc-13 image builds against (a GLIBCXX version mismatch otherwise),
# and pulling in a matching newer libstdc++6 in the runtime stage would
# mean tracking that version by hand. Everything else (libssl, libpq,
# libc) still links dynamically against the runtime stage's own copies.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" \
    && cmake --build build --target forge forge-server forge-worker

FROM debian:bookworm-slim AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends libssl3 libpq5 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --home-dir /app --shell /usr/sbin/nologin forge

WORKDIR /app
COPY --from=build /workspace/build/forge /usr/local/bin/forge
COPY --from=build /workspace/build/forge-server /usr/local/bin/forge-server
COPY --from=build /workspace/build/forge-worker /usr/local/bin/forge-worker
COPY --from=build /workspace/migrations /app/migrations

# forge-repos/forge-data are created under /app on first run (see
# main_server.cpp's create_directories) — owned by the unprivileged
# `forge` user so the process never runs as root.
RUN mkdir -p /app/forge-repos /app/forge-data /app/forge-backups \
    && chown -R forge:forge /app

USER forge

# No EXPOSE/published port here on purpose: in deploy/docker-compose.prod.yml,
# forge-server is reached only by the caddy service over the compose-internal
# network, never bound to a host port directly (see deploy/README.md).
ENTRYPOINT ["forge-server"]

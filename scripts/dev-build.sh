#!/usr/bin/env bash
# Build and test Forge inside the Docker dev toolchain (gcc-13 + cmake + ninja).
# Use this until a C++20 toolchain is installed directly on the host.
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_TYPE="${1:-Debug}"

# On Git Bash / MSYS, disable automatic path conversion so paths like
# "/workspace" are passed through to the container unchanged.
export MSYS_NO_PATHCONV=1

docker build -q -t forge-dev -f Dockerfile.dev . >/dev/null

docker run --rm -v "$(pwd):/workspace" -w /workspace forge-dev bash -lc "
  set -euo pipefail
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$BUILD_TYPE
  cmake --build build
  ctest --test-dir build --output-on-failure
"

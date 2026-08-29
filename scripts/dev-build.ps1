# Build and test Forge inside the Docker dev toolchain (gcc-13 + cmake + ninja).
# Use this until a C++20 toolchain is installed directly on the host.
param(
    [string]$BuildType = "Debug"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    docker build -q -t forge-dev -f Dockerfile.dev . | Out-Null

    docker run --rm -v "${repoRoot}:/workspace" -w /workspace forge-dev bash -lc "
        set -euo pipefail
        cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$BuildType
        cmake --build build
        ctest --test-dir build --output-on-failure
    "
} finally {
    Pop-Location
}

# Scripts

Developer tooling, not part of the Forge product.

- `dev-build.sh` / `dev-build.ps1` — build and run the test suite inside the
  Docker dev toolchain (`Dockerfile.dev`, gcc-13 + cmake + ninja). Use this
  until a C++20 toolchain is installed directly on the host.

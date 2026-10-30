# Contributing

- Clone with `git clone --recurse-submodules`, or run `git submodule update --init` (the PC build needs only `third_party/ed25519`).
- Build and test: `sh build.sh && ./out/selftest`, or `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
- Keep `core/` and `protocols/` portable: C++17 for the PC/CMake build, and C++11-compatible because the QuecOpen build uses the SDK's `-std=c++11`. No exceptions/RTTI, no heap-heavy containers, no platform headers. Everything platform-specific goes behind `core/port.h` (see `docs/PORTING.md`).
- A new or changed port must pass `test/port_conformance.cpp` and list its deviations in `ports/README.md` (see `docs/PORT_CONTRACT.md`).
- Protocol/crypto changes need a test vector in `test/selftest.cpp` (from tsclientlib or a captured exchange).
- Do not commit identities (`identity*.txt`, `id_*.txt`), device flash dumps, or captured audio.
- Mark deliberate shortcuts with a `ponytail:` comment naming the limit and the upgrade path.
- Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/): `type(scope): summary`, e.g. `feat(ts3): ...`, `fix(port-pc): ...`, `docs: ...`, `build: ...`, `chore: ...`.
- By contributing you agree your work is released under the MIT license (`LICENSE`).

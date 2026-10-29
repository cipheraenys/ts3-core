# Contributing

- Build and test: `sh build.sh && ./out/selftest`, or `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
- Keep `src/` portable C++17: no exceptions/RTTI, no heap-heavy containers, no platform headers. Everything platform-specific goes behind `src/port.h` (see `docs/PORTING.md`).
- A new or changed port must pass `test/port_conformance.cpp` and list its deviations in `ports/README.md` (see `docs/PORT_CONTRACT.md`).
- Protocol/crypto changes need a test vector in `test/selftest.cpp` (from tsclientlib or a captured exchange).
- Do not commit identities (`identity*.txt`, `id_*.txt`), device dumps (`hw/`), or captured audio.
- Mark deliberate shortcuts with a `ponytail:` comment naming the limit and the upgrade path.
- By contributing you agree your work is released under the MIT license (`LICENSE`).

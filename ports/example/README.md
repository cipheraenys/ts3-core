# Port template

`port_stub.cpp` implements every function in `core/port.h` with neutral values so a new port compiles and links before any hardware code exists. Nothing in it works: `port_udp_open` returns -1, `port_unix_time` returns 0, `port_random` zero-fills.

## Use

1. Copy it to `ports/<target>/port_<target>.cpp` (or next to your RTOS app) and replace each body, following [docs/PORT_CONTRACT.md](../../docs/PORT_CONTRACT.md).
2. Run `test/port_conformance.cpp` against it (instructions at the end of the contract). Every check must pass.
3. Review `port_random` and `port_unix_time` by hand: the test cannot prove entropy or clock accuracy.
4. Add `ports/<target>/README.md` with what is implemented, how to build, and what was actually run.

## State of the template

`cmake -S . -B build -DTS3_PORT=stub` builds it. `ctest` then fails `port_conformance` (and `selftest`, which aborts with the stub). That is the intended starting point: the failures list what is left to implement.

# Port template

`port_stub.cpp` implements every function in `core/port.h` with neutral values so a new port compiles and links before any hardware code exists. Nothing in it works: `port_udp_open` returns -1, `port_unix_time` returns 0, `port_random` zero-fills.

## Use

Copy it to `ports/<dir>/port_<name>.cpp` and follow [docs/PORTING.md](../../docs/PORTING.md).

## State of the template

`cmake -S . -B build -DTS3_PORT=stub` builds it. `ctest` then fails `port_conformance` (and `selftest`, which aborts with the stub). That is the intended starting point: the failures list what is left to implement.

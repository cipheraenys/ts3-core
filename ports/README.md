# Ports

A port is one C++ file that defines the eight functions in `core/port.h`, plus a README saying what was implemented and what was actually run. The contract is [docs/PORT_CONTRACT.md](../docs/PORT_CONTRACT.md); `test/port_conformance.cpp` checks what can be checked.

Every port lives in `ports/<dir>/` with one `port_<name>.cpp`. Sources include `"port.h"` and `"ts3.h"` without a path; each build adds `-I core -I protocols/ts3`.

| `-DTS3_PORT=` | File | Transport | Build | Status |
|---|---|---|---|---|
| `pc` | `ports/pc/port_pc.cpp` | BSD sockets / Winsock | `build.sh`, CMake | tested: selftest and `port_conformance` pass on Debian trixie; `ts3cli` against `teamspeak:latest` (see top-level README) |
| `freertos` | `ports/freertos-lwip/port_freertos.cpp` | FreeRTOS+TCP | `ports/freertos-lwip/Makefile` | tested in QEMU `mps2-an385` only; `port_random` does not meet the contract |
| `quecopen` | `ports/quecopen-ec600m/port_quecopen.cpp` | lwIP sockets (QuecOpen SDK) | `ports/quecopen-ec600m/BUILD.md` | builds and links; not run on hardware |
| `stub` | `ports/example/port_stub.cpp` | none | CMake | template; compiles, does nothing |

CMake: `cmake -S . -B build -DTS3_PORT=pc` (default). `pc` and `stub` build `selftest` and `port_conformance` for the host. `freertos` and `quecopen` build the static libraries only (no tests, no CLI); they need a cross toolchain, the RTOS/SDK headers and a cross-built mbedTLS (`-DCMAKE_TOOLCHAIN_FILE=...`, `-DTS3_PORT_INCLUDE_DIRS="dir1;dir2"`, `-DMBEDTLS_INCLUDE_DIR`, `-DMBEDCRYPTO_LIBRARY`). The real firmware builds stay with their own makefiles.

## pc

Development and test port. Uses `getaddrinfo`/`select`/`recv` and `std::random_device`. `std::random_device` may be deterministic on old MinGW-w64 (fixed in GCC 9.2), see the contract.

## freertos (QEMU)

FreeRTOS + FreeRTOS+TCP on `mps2-an385` (Cortex-M3, LAN9118), reusing the official `FreeRTOS_Plus_TCP_Echo_Qemu_mps2` demo for startup and DHCP. Details and measurements: top-level README.

Conformance on this port: `cd ports/freertos-lwip && make CONF_PORT=7777 run`, with a UDP echo server on the host (the guest reaches the host at `10.0.2.2`):

```sh
python3 -c "import socket; s=socket.socket(2,2); s.bind(('0.0.0.0',7777))
while True: d,a=s.recvfrom(4096); s.sendto(d,a)"
```

Conformance passes intermittently: of 3 runs, two reported `33 checks, 0 failed` (one after a boundary-check retry), and in one the two-datagram boundary check failed all 3 attempts. Cause not investigated (QEMU LAN9118/slirp, FreeRTOS+TCP ARP, or the port). The test retries up to 3 times because FreeRTOS+TCP drops datagrams sent while it resolves the peer's MAC by ARP.

Deviations from the contract:

- `port_random` is the demo's `xApplicationGetRandomNumber`: an LCG seeded from the boot-time tick count (one boot logged `Seed for randomiser: 32`) and mixed with the current tick, so its output is guessable. It passes the conformance statistics anyway (see the contract). Replace it with a hardware TRNG before any use outside QEMU.
- `port_unix_time` is the build time plus uptime, not a clock.
- `port_udp_open` takes dotted IPv4 only, at most 4 sockets, and `port_udp_recv` does not check the datagram's source address.

## quecopen

See [quecopen-ec600m/README.md](quecopen-ec600m/README.md).

## stub

See [example/README.md](example/README.md).

## Adding a port

Follow [docs/PORTING.md](../docs/PORTING.md). Then add `ports/<dir>/README.md` (implemented with, build, what was run, deviations), a row to the table above and, if it builds from CMake, a `TS3_PORT` value in the top-level `CMakeLists.txt`.

A port is listed as "tested" only after the conformance test and the self-test ran on it. Anything else is "builds" or "template".

# Ports

A port is one C++ file that defines the eight functions in `src/port.h`, plus a README saying what was implemented and what was actually run. The contract is [docs/PORT_CONTRACT.md](../docs/PORT_CONTRACT.md); `test/port_conformance.cpp` checks what can be checked.

New ports go in `ports/<target>/port_<target>.cpp`. Two older ports stay where their build scripts reference them (`build.sh` uses `pc/`, `rtos/freertos_qemu/Makefile` uses its own directory), so they were not moved.

| `-DTS3_PORT=` | File | Transport | Build | Status |
|---|---|---|---|---|
| `pc` | `pc/port_pc.cpp` | BSD sockets / Winsock | `build.sh`, CMake | tested: selftest and `port_conformance` pass on Debian trixie; `ts3cli` against `teamspeak:latest` (see top-level README) |
| `freertos` | `rtos/freertos_qemu/port_freertos.cpp` | FreeRTOS+TCP | `rtos/freertos_qemu/Makefile` | tested in QEMU `mps2-an385` only; `port_random` does not meet the contract |
| `quecopen` | `ports/quecopen/port_quecopen.cpp` | lwIP sockets (QuecOpen SDK) | `ports/quecopen/BUILD.md` | builds and links; not run on hardware |
| `stub` | `ports/example/port_stub.cpp` | none | CMake | template; compiles, does nothing |

CMake: `cmake -S . -B build -DTS3_PORT=pc` (default). `pc` and `stub` build `selftest` and `port_conformance` for the host. `freertos` and `quecopen` build only `libts3c_port.a`; they need a cross toolchain and the RTOS/SDK headers (`-DCMAKE_TOOLCHAIN_FILE=...`, `-DTS3_PORT_INCLUDE_DIRS="dir1;dir2"`). The real firmware builds stay with their own makefiles.

## pc

Development and test port. Uses `getaddrinfo`/`select`/`recv` and `std::random_device`. `std::random_device` may be deterministic on old MinGW-w64 (fixed in GCC 9.2), see the contract.

## freertos (QEMU)

FreeRTOS + FreeRTOS+TCP on `mps2-an385` (Cortex-M3, LAN9118), reusing the official `FreeRTOS_Plus_TCP_Echo_Qemu_mps2` demo for startup and DHCP. Details and measurements: top-level README.

Conformance on this port: `cd rtos/freertos_qemu && make CONF_PORT=7777 run`, with a UDP echo server on the host (the guest reaches the host at `10.0.2.2`):

```sh
python3 -c "import socket; s=socket.socket(2,2); s.bind(('0.0.0.0',7777))
while True: d,a=s.recvfrom(4096); s.sendto(d,a)"
```

Observed over 3 runs of the final test: `port_conformance: 33 checks, 0 failed` twice (once after a boundary-check retry), and once the two-datagram boundary check failed all 3 attempts (the second datagram never came back). The cause was not investigated; the QEMU LAN9118/slirp path, FreeRTOS+TCP ARP handling and the port are all candidates. Treat this port as "conformance passes intermittently". The test retries up to 3 times because FreeRTOS+TCP drops datagrams sent while it is still resolving the peer's MAC address by ARP; the core tolerates that through its own retransmits.

Deviations from the contract:

- `port_random` is the demo's `xApplicationGetRandomNumber`, an LCG seeded from the tick count at boot (the log line `Seed for randomiser: 32` appeared in the boot we looked at), mixed with the current tick. The seed is a boot-time tick count, a tiny search space, so the output is guessable. It passes the conformance statistics anyway; see the contract for why that proves nothing. Replace it with the SoC TRNG before any use outside QEMU.
- `port_unix_time` is the build time plus uptime, not a clock.
- `port_udp_open` takes dotted IPv4 only, at most 4 sockets, and `port_udp_recv` does not check the datagram's source address.

## quecopen

See [quecopen/README.md](quecopen/README.md).

## stub

See [example/README.md](example/README.md).

## Adding a port

1. Copy `ports/example/port_stub.cpp` to `ports/<target>/port_<target>.cpp`.
2. Implement it against the contract and run `port_conformance` (host runner, or `port_conformance_run()` from a task on the device).
3. Add `ports/<target>/README.md` (implemented with, build, what was run, deviations) and a row to the table above.
4. If it builds from CMake, add a `TS3_PORT` value in the top-level `CMakeLists.txt`.

A port is listed as "tested" only after the conformance test and the self-test ran on it. Anything else is "builds" or "template".

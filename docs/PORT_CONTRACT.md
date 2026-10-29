# Port contract: `src/port.h`

Copyright (c) 2026 cipheraenys@gmail.com, MIT license.

`src/port.h` declares the eight functions the core (`src/ts3.cpp`, `src/crypto.cpp`) calls to reach the outside world. Anything that implements them can run the core. This file says what "implements" means; `test/port_conformance.cpp` checks the parts that can be checked.

Words in capitals follow [RFC 2119](https://www.rfc-editor.org/rfc/rfc2119).

## General rules

- The functions have C++ linkage (`port.h` has no `extern "C"`). Implement them in a `.cpp` file or match the mangling.
- The core builds with `-fno-exceptions -fno-rtti`. A port MUST NOT throw.
- The core calls every function from the one task that runs `Ts3Client::poll()`. A port does not need to be reentrant, except `port_log` if your application also logs from other tasks.
- `port_udp_recv` blocks the calling task only. It SHOULD yield the CPU while waiting (RTOS queue/semaphore wait, `select`), not spin.
- Handles are opaque `int`s. The core only tests `>= 0` (`ts3.cpp:66`, `ts3.cpp:102`).

## `int port_udp_open(const char *host, uint16_t port)`

Creates a UDP socket whose peer is `host:port`.

- `host` is a NUL-terminated string. A dotted-quad IPv4 address MUST work. Hostnames and IPv6 are optional; a port that lacks them returns `< 0`, it does not guess.
- Return a handle `>= 0`, or `< 0` on any failure (bad address, no network, no free socket, DNS failure).
- The local port is chosen by the stack (ephemeral). The core never needs to know it.
- "Connected" semantics: `send` goes to the peer, and the port SHOULD discard datagrams from other sources. POSIX `connect()` on a `SOCK_DGRAM` socket does both ([POSIX connect](https://pubs.opengroup.org/onlinepubs/9699919799/functions/connect.html)). A port that only calls `sendto`/`recvfrom` (the FreeRTOS port) is still correct: the core authenticates every packet with EAX, so foreign datagrams are dropped by the crypto, at the cost of CPU.
- MUST be callable repeatedly: the core closes and reopens the socket on an init restart (`ts3.cpp:318`). Closed handles MUST become reusable, so a fixed-size table does not leak.
- At least one handle MUST be open at a time. The core uses exactly one.
- The network may not be up yet. Returning `< 0` is correct then; the core logs it and `connect()` fails. (On QuecOpen the data call must exist first, see `ports/quecopen/BUILD.md`.)

## `void port_udp_close(int s)`

Releases the handle. `s` is always a handle previously returned `>= 0` (`ts3.cpp:102` guards). After the call the handle value may be returned again by `port_udp_open`.

## `int port_udp_send(int s, const void *buf, size_t len)`

Sends one datagram of `len` bytes.

- Return `len` on success, `< 0` on error. UDP has no partial send.
- The core ignores the return value and recovers by its own retransmit timer, so dropping a packet is acceptable. Blocking indefinitely is not. FreeRTOS+TCP drops datagrams sent while it resolves the peer's MAC by ARP; the conformance test retries for this reason.
- The largest datagram the core sends is `MAX_PKT = 500` bytes (`ts3.cpp:17`), below the 576-byte minimum IPv4 reassembly size, so the port never needs to fragment.

## `int port_udp_recv(int s, void *buf, size_t len, uint32_t timeout_ms)`

Waits up to `timeout_ms` for one datagram and copies it into `buf`.

| Return | Meaning |
|---|---|
| `n > 0` | one datagram of `n` bytes; datagram boundaries are preserved |
| `0` | timeout, nothing received |
| `< 0` | error |

- `timeout_ms == 0` is a non-blocking poll. The core drains the socket with it (`ts3.cpp:122-123`) and stops at the first result `<= 0`.
- The core passes `len = MAX_PKT + 16 = 516` and its own timeouts are 0 to 50 ms; ports SHOULD still handle any `uint32_t` without overflowing a tick or `timeval` conversion.
- Early return: a port MAY return `0` somewhat before `timeout_ms` (tick granularity, `select` rounding). The core's timers are driven by `port_millis`, not by this return. The conformance test accepts 80 ms or more for a 100 ms request.
- A datagram longer than `len` is unspecified: POSIX `recv` silently discards the excess ([POSIX recv](https://pubs.opengroup.org/onlinepubs/9699919799/functions/recv.html)), Winsock `recv` fails with `WSAEMSGSIZE` and loses the data ([Winsock recv](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-recv)). Either is acceptable. Writing past `len` is not.
- A zero-length datagram may surface as `0` and be treated as a timeout. The protocol has none.
- Asynchronous errors: on Linux, a connected UDP socket reports ICMP "port unreachable" as `ECONNREFUSED` on a later call ([udp(7)](https://man7.org/linux/man-pages/man7/udp.7.html)). The port returns `< 0` for it. The core does not close the socket on `< 0`; it ends its drain loop and polls again. A port whose socket fails permanently should therefore also pause (not return instantly forever), or `poll()` degenerates into a busy loop.
- How to wait on common stacks:
  - POSIX / Winsock / lwIP sockets: `select()` with a `timeval`, or `SO_RCVTIMEO` ([POSIX select](https://pubs.opengroup.org/onlinepubs/9699919799/functions/select.html), [POSIX setsockopt](https://pubs.opengroup.org/onlinepubs/9699919799/functions/setsockopt.html)). lwIP implements both, but `SO_RCVTIMEO` exists only when `LWIP_SO_RCVTIMEO` is 1 and its default is 0; `select` depends on `LWIP_SOCKET_SELECT` (default 1) ([lwIP socket options](https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__socket.html), [lwIP socket API](https://www.nongnu.org/lwip/2_1_x/group__socket.html)).
  - FreeRTOS+TCP: `FREERTOS_SO_RCVTIMEO` in ticks via `FreeRTOS_setsockopt`; on timeout `FreeRTOS_recvfrom` returns `-pdFREERTOS_ERRNO_EWOULDBLOCK` (see `FreeRTOS_Sockets.c` in [FreeRTOS-Plus-TCP](https://github.com/FreeRTOS/FreeRTOS-Plus-TCP/blob/main/source/FreeRTOS_Sockets.c)), which `port_freertos.cpp` maps to `0`.

## `uint32_t port_millis(void)`

Milliseconds from a monotonic clock, arbitrary origin (boot is fine), wrapping at 2^32 (about 49.7 days).

- MUST NOT go backwards except by that wrap. In particular it MUST NOT be derived from the RTC or `port_unix_time`: NITZ/NTP/RTC adjustments step wall time, and every retransmit, ping and timeout in the core (`ts3.cpp:129-146`) is computed from this clock. POSIX names this property `CLOCK_MONOTONIC` ([clock_gettime](https://pubs.opengroup.org/onlinepubs/9699919799/functions/clock_gettime.html)); C++ calls it `steady_clock` ([cppreference](https://en.cppreference.com/w/cpp/chrono/steady_clock)).
- Resolution: 10 ms or better. The core's retransmit and ping timers are 1000 ms and up, but applications pace 20 ms Opus frames from it (`pc/main.cpp`, `rtos/freertos_qemu/app.cpp`); a 100 Hz tick is the practical floor, 1 kHz is what the QEMU demo uses (`configTICK_RATE_HZ`).
- Elapsed-time arithmetic in the core (`now - t0`) is wrap-safe. Absolute deadline comparisons (`now < p.next`, `ts3.cpp:136`) are not, so a session that spans the wrap can stall retransmits briefly. Starting at 0 at boot makes this a 49.7-day-uptime issue.

## `uint32_t port_unix_time(void)`

Seconds since 1970-01-01T00:00:00 UTC, excluding leap seconds ([POSIX "Seconds Since the Epoch"](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap04.html#tag_04_19)). Wall time, so it MAY jump (NTP, NITZ).

- The core reads it once per `connect()`, puts it into the first handshake packet and derives `version_ = now - 1356998400` from it (`ts3.cpp:85-89`). It does not check the value locally (license expiry is not checked, see README). What a given server does with a wrong timestamp was not tested here.
- If no real time is available (RTC unset, cellular network time not yet received), return a best-effort value such as build time plus uptime, as `port_freertos.cpp` does. Returning `0` is possible but makes `version_` wrap as an unsigned subtraction; `port_conformance` fails it (it requires a value after 2025-01-01).
- The conformance bound (2025-01-01) is a "clock is set at all" check, not an accuracy check.

## `void port_random(void *buf, size_t len)`

Fills exactly `len` bytes of `buf` with output from a cryptographically secure source seeded by real entropy: a hardware TRNG, or the OS CSPRNG (`getrandom(2)`, `BCryptGenRandom`, ...).

**Why this is not optional.** The core takes all key material from here:

| Use | Where |
|---|---|
| ephemeral key that derives the packet encryption IV/MAC (`ek_create` -> `ek_shared`) | `crypto.cpp:185-187` |
| long-term P-256 identity key (`mbedtls_ecp_gen_key`) and ECDSA blinding/nonces (`rng` callback) | `crypto.cpp:224-270` |
| handshake nonces `random0_`, `alpha_` | `ts3.cpp:87`, `ts3.cpp:337` |

If an attacker can enumerate the generator's outputs, they can recompute the ephemeral private key from a captured handshake and with it the session keys (decrypt and forge traffic); a guessable identity key lets them impersonate the device; and ECDSA signing with predictable nonces leaks the signing key (mbedTLS draws the nonce from the RNG callback unless built with `MBEDTLS_ECDSA_DETERMINISTIC`). A PRNG seeded from a boot-time tick count has a search space of a few thousand values and, on a deterministic boot, may repeat the same keys. This class of failure is catalogued as [CWE-338](https://cwe.mitre.org/data/definitions/338.html) and [CWE-330](https://cwe.mitre.org/data/definitions/330.html), and has happened in practice: [Debian's OpenSSL generator was reduced to a few thousand possible keys (DSA-1571)](https://www.debian.org/security/2008/dsa-1571), and [embedded devices generated duplicate or factorable keys at first boot](https://factorable.net/paper.html). Requirements for entropy sources are in [NIST SP 800-90B](https://csrc.nist.gov/pubs/sp/800/90/b/final) and [RFC 4086](https://www.rfc-editor.org/rfc/rfc4086).

- The signature has no error channel. If the source fails or is not ready, block, reset or halt. Never return with `buf` unfilled or filled from a fallback PRNG.
- `len == 0` writes nothing. `buf` may be unaligned. The core's own calls ask for 4 to 64 bytes; mbedTLS chooses the sizes for its callback.
- On mbedTLS-based ports, `mbedtls_hardware_poll()` is the hook for a platform entropy source ([mbedTLS entropy API](https://mbed-tls.readthedocs.io/projects/api/en/v3.6.0/api/file/entropy_8h/)); you can feed the same source to `port_random`.
- `std::random_device` is not a guarantee: the standard allows a deterministic fallback, and old MinGW-w64 had one (fixed in GCC 9.2) ([cppreference](https://en.cppreference.com/w/cpp/numeric/random/random_device)). `pc/port_pc.cpp` uses it; it is acceptable for development, check your toolchain for anything else.

The conformance test runs statistical sanity checks (byte distribution, bit balance, repeats, period shorter than 4 KiB, consecutive calls differ). They reject constants, counters and short-period generators. They do not prove entropy: the seeded LCG used by the FreeRTOS demo's `xApplicationGetRandomNumber` passes them (checked, see `ports/README.md`). Review the source, not the test result.

## `void port_log(const char *fmt, ...)`

printf-style diagnostics, one line per call.

- The port appends the newline; callers do not.
- `fmt` is always a string literal in this repo. Formats used: `%d %u %x %c %s` and `%.Ns` (`pc/main.cpp:74`). `%llu` and `%lld` MUST NOT be used by callers: newlib-nano's printf does not support them ([newlib mailing list](https://sourceware.org/pipermail/newlib/2026/022568.html), [newlib nano-formatted-io note](https://sourceware.org/legacy-ml/newlib/2014/msg00169.html)); use `ts3c::u64_dec()`.
- Output MAY be truncated (`port_quecopen.cpp` formats into 256 bytes) but MUST NOT overflow a buffer. Output MAY be dropped entirely (the stub does).
- It is called during start-up before the network exists, so it must not depend on a socket.

## What `test/port_conformance.cpp` checks

| Area | Check |
|---|---|
| open/close | 8 open/close cycles succeed; an unresolvable `.invalid` host ([RFC 6761 6.4](https://www.rfc-editor.org/rfc/rfc6761#section-6.4)) returns `< 0` |
| recv timeout | 100 ms wait with nothing pending returns `0` after 80 to 1000 ms by `port_millis`; `timeout 0` returns `0` in under 50 ms |
| send/recv | echoes of 1, 100 and 500 bytes arrive intact with the right length; two datagrams of 10 and 20 bytes stay separate; the socket then times out cleanly |
| `port_millis` | 200000 consecutive reads, none goes backwards |
| `port_unix_time` | at least 2025-01-01; does not go backwards or jump more than 2 s over the test's 100 ms wait |
| `port_random` | chi-square over 4096 bytes, distinct byte values, bit balance, adjacent-equal bytes, no repeated 16-byte block, two consecutive calls differ, `len == 0` writes nothing, unaligned destination |
| `port_log` | formats `%d %u %x %c %s` and a 400-character line do not crash |

Not covered: entropy quality, RTC accuracy, 32-bit wrap behavior, behavior under a second task, truncation of oversized datagrams, source filtering, ICMP error mapping. These stay a review item per port.

## Running it on a new port

Host-like target (a POSIX or Winsock port), from the repo root:

```sh
g++ -std=c++17 test/port_conformance.cpp test/port_conformance_pc.cpp path/to/port_x.cpp -o port_conformance && ./port_conformance
```

`port_conformance_pc.cpp` starts its own UDP echo peer on `127.0.0.1`. With `./port_conformance HOST PORT` it uses an external echo server instead.

On a device, link `test/port_conformance.cpp` into the firmware and call it from a task after the network is up:

```cpp
#include "test/port_conformance.h"
int fails = port_conformance_run("192.0.2.10", 7777, nullptr);  // 0 = pass; results go to port_log
```

and run a UDP echo server on that host, for example:

```python
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("0.0.0.0", 7777))
while True:
    d, a = s.recvfrom(4096); s.sendto(d, a)
```

`rtos/freertos_qemu/` has this wired up: `make CONF_PORT=7777` (see `ports/README.md`).

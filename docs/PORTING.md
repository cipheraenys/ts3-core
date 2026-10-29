# Porting to a new chip / RTOS

`core/port.h` is the only platform seam. `protocols/ts3/`, `core/crypto.*` and `third_party/ed25519` are portable and must not change per target.

## 1. Implement `core/port.h`

Start from `ports/example/port_stub.cpp` (compiles, returns neutral values) or a working reference: `ports/pc/port_pc.cpp` (sockets) and `ports/freertos-lwip/port_freertos.cpp` (FreeRTOS+TCP). The exact semantics, with references, are in [PORT_CONTRACT.md](PORT_CONTRACT.md); the table below is the summary.

| Function | Contract | Typical source |
|---|---|---|
| `port_udp_open(host, port)` | UDP socket "connected" to host:port; handle >= 0 or < 0 on error | lwIP/BSD socket, `connect()` |
| `port_udp_send(s, buf, len)` | send one datagram | `send()` |
| `port_udp_recv(s, buf, len, timeout_ms)` | bytes read, 0 on timeout, < 0 on error | `select()` / `SO_RCVTIMEO` |
| `port_udp_close(s)` | close | `close()` |
| `port_millis()` | monotonic ms, 32-bit wrap OK | RTOS tick |
| `port_unix_time()` | seconds since epoch (license validity, handshake) | RTC / NTP / cell network time |
| `port_random(buf, len)` | must be real entropy on device | hardware TRNG |
| `port_log(fmt, ...)` | printf-style log | UART / RTT |

Gotcha: newlib-nano `printf` has no `%llu`; use `ts3c::u64_dec()` for 64-bit numbers.

## 2. Build

- Sources: `protocols/ts3/ts3.cpp`, `core/crypto.cpp`, `third_party/ed25519/src/{fe,ge,sc}.c` (git submodule), your port file.
- Compile C++ with `-std=c++17 -fno-exceptions -fno-rtti`.
- mbedTLS 3.6 with: `CMAC_C`, `CIPHER_MODE_CTR`, `ECDSA_C`, `ECP_DP_SECP256R1`, `SHA1/256/512`, `BASE64_C`
  (see `ports/freertos-lwip/ts3_mbedtls_config.h` for a working config).
- libopus, fixed-point build on cores without FPU (`-DFIXED_POINT -DDISABLE_FLOAT_API`).

## 3. Identity

Create it once on a PC: `ts3cli` writes `identity.txt` (private key hex + hashcash offset). Flash that file's contents to the device and keep it out of version control.

## 4. Run loop

One task: `client.poll(10)`. While PTT is held, encode one frame every 20 ms (960 samples at 48 kHz, 320 at 16 kHz) and call `send_voice()`; on release `send_voice(nullptr, 0)`. Feed `on_voice` frames to the decoder and speaker.

## Footprint

`Ts3Client` is about 81 KB RAM (mostly `kFragMax`/`kCmdMax` reassembly buffers) plus a 16 KB QuickLZ table and Opus state.
Default member values put the client object in `.data`, so it also costs that much flash.

## Check

Run `test/selftest.cpp` on the device (the QEMU demo does). It exercises crypto against tsclientlib test vectors.

Run `test/port_conformance.cpp` too: it checks the port itself (UDP echo and timeout, clock monotonicity, entropy sanity, time sanity). Usage is at the end of [PORT_CONTRACT.md](PORT_CONTRACT.md). It cannot prove that `port_random` is a real entropy source.

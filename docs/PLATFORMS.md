# Platforms

`core/` is portable C++17 and depends only on mbedTLS and ed25519. A target is
an implementation of the eight-function seam in `core/port.h`; nothing else
differs per device. TS3 is UDP and does **not** open a TLS session, so there are
no 16 KB TLS record buffers — the RAM cost is the ECP P-256 context, the Opus
state, and socket buffers.

## Feasibility by device

| Device | Cores / clock | RAM | Tier |
|---|---|---|---|
| EC600M / ASR1606 (BQ280) | Cortex-R4 | modem heap | voice (fixed-point Opus) — current target |
| ESP32 / ESP32-S3 | 2 × 240 MHz | 320–520 KB SRAM (+PSRAM) | voice; jitter buffer in PSRAM |
| ESP8266 | 1 × 80–160 MHz | ~40 KB heap | text / signaling only, no Opus |
| generic FreeRTOS + lwIP | — | ≥ ~200 KB | per the tiers below |

### Tiers

- **voice** — full client including Opus. Needs a codec thread with **≥ 30 KB
  stack** (Opus encode/decode use `alloca`), about 70–80 % of one 240 MHz core
  for 16 kHz fixed-point encode, and a 1–2 s jitter buffer best placed in PSRAM.
- **text / signaling** — handshake, channels, text; no Opus. Fits small heaps.
  The crypto still runs (AES/SHA, ECP P-256, the MPI puzzle, ed25519); ECP is the
  heaviest RAM user, still far below a full TLS session.

## The seam (`core/port.h`)

| Function | Provide with |
|---|---|
| `port_udp_open` / `close` / `send` / `recv` | lwIP BSD sockets, or the platform's UDP API |
| `port_millis` | monotonic RTOS tick |
| `port_unix_time` | RTC / NTP / cellular time — **must be set before connect**, Init1 signs a timestamp |
| `port_random` | a real hardware entropy source |
| `port_log` | UART / console, or a no-op |

This follows HAL practice: inject time and RNG, keep the core RTOS-agnostic, keep
it host-testable. The `pc` port plus `test/selftest` run the whole core on a
workstation.

## Adding a target

1. Copy `ports/example/` to `ports/<target>/` and implement the eight functions.
2. Pick a tier. For **voice**, give the codec thread ≥ 30 KB stack and link Opus
   (fixed-point). For **text-only**, build without Opus.
3. Wire `port_udp_*` to the platform's sockets, `port_random` to hardware RNG,
   `port_unix_time` to the clock.
4. Run `test/port_conformance` against a UDP echo to prove the seam, then
   `test/selftest` for the crypto vectors.

### ESP32 (ESP-IDF)

ESP-IDF already ships FreeRTOS + lwIP BSD sockets + mbedTLS, so the
`freertos-lwip` port maps over almost directly. Add Opus with the `esp-libopus` /
`esp-opus` component, give its thread ≥ 30 KB stack, put the jitter buffer in PSRAM.

### ESP8266

Text / signaling tier only. ~40 KB heap does not hold Opus plus the crypto
working set; build without Opus and keep socket buffers small.

## Grounding

- Opus on ESP32 (stack, CPU): <https://github.com/XasWorks/esp-libopus>, <https://getstream.io/blog/stream-video-esp32/>
- ESP8266 heap limits: <https://nodemcu.readthedocs.io/en/release/modules/tls/>
- HAL / port design: <https://www.embeddedrelated.com/showarticle/1596.php>

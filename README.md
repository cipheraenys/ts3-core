# TS3-RTOS

Minimal TeamSpeak 3 client core in portable C++17, aimed at RTOS PoC radios.
Protocol follows ReSpeak's `tsdeclarations` spec and `tsclientlib` (MIT/Apache-2.0).

## Status (tested against official `teamspeak:latest` Docker server)

- Init1 handshake + RSA puzzle (level 10000, ~4 ms on PC)
- `initivexpand2` / `clientek`: license chain (Ed25519), P-256 identity, EAX encryption
- `clientinit` → `initserver`, channel list (fragmented + QuickLZ), ping/pong, resend, clean disconnect
- Voice TX/RX (Opus): verified with two clients, 440 Hz tone in → 440.0 Hz out
- 60 s soak: stays connected

## Layout

```
core/                 protocol-agnostic: port.h (platform seam), crypto (EAX, license chain, P-256, puzzle, QuickLZ)
protocols/ts3/        TeamSpeak 3 protocol (handshake, commands, voice packets)
ports/                one directory per target, each implements core/port.h
  pc/                 Linux / Windows (MinGW) + ts3cli test client
  freertos-lwip/      FreeRTOS + FreeRTOS+TCP on QEMU mps2-an385
  quecopen-ec600m/    Quectel EC600M, QuecOpen C-SDK (port, app, SDK integration, BUILD.md)
  example/            template port
test/                 selftest (tsclientlib vectors), port_conformance (checks a port.h implementation)
docs/                 PORT_CONTRACT.md, PORTING.md, PLATFORMS.md
third_party/          git submodules: mbedtls, opus, ed25519
build.sh              PC build (kept at the root: it is the documented one-liner)
CMakeLists.txt        PC / stub build, `-DTS3_PORT=...`
```

Dependency direction: `ports/*` and `protocols/*` depend on `core/`; `core/` depends on nothing but mbedTLS and ed25519.

## Get the sources

```sh
git clone --recurse-submodules <repo>             # or: git submodule update --init
git submodule update --init third_party/ed25519   # minimum for the PC build
```

| Submodule | Version | Used by |
|---|---|---|
| `third_party/ed25519` | orlp/ed25519 `b1f19fa` | every build (`src/{fe,ge,sc}.c`) |
| `third_party/mbedtls` | v3.6.6 | `freertos-lwip`, `quecopen-ec600m` (the PC build uses the system mbedTLS 3.x) |
| `third_party/opus` | v1.5.2 | `freertos-lwip`, `quecopen-ec600m` (the PC build uses the system libopus) |

FreeRTOS and FreeRTOS-Plus-TCP stay outside the repo (`~/ts3deps`). The QuecOpen SDK and the QuecPython firmware are vendor downloads and cannot be submodules.

## Build & test (PC)

Requires mbedTLS 3.x, libopus and the `third_party/ed25519` submodule. `build.sh` (MSYS2 ucrt64 or Linux) and CMake both work:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

```sh
docker run -d --name ts3srv -e TS3SERVER_LICENSE=accept -p 9987:9987/udp teamspeak
sh build.sh && ./out/selftest
./out/ts3cli 127.0.0.1 -n Receiver -i id_b.txt -t 15 --rx rx.wav &
./out/ts3cli 127.0.0.1 -n Talker   -i id_a.txt -t 8 --tone 3
```

## FreeRTOS in QEMU (WSL)

`ports/freertos-lwip/`: FreeRTOS + FreeRTOS+TCP on QEMU `mps2-an385` (Cortex-M3, LAN9118). It reuses the
official `FreeRTOS_Plus_TCP_Echo_Qemu_mps2` demo for startup, UART and DHCP, and runs the self-test and
then a TS3 **echo bot** that plays back every voice frame it hears.

```sh
# WSL, once: apt install qemu-system-arm gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib make
#            git clone FreeRTOS (+ Source, FreeRTOS-Plus-TCP submodules) into ~/ts3deps
#            git submodule update --init third_party/mbedtls third_party/opus third_party/ed25519
cd ports/freertos-lwip && make -j && make run      # guest reaches the host's TS3 at 10.0.2.2
./out/ts3cli 127.0.0.1 -n Talker --tone 3 --rx echo.wav -t 9   # echo.wav = audio that went through the RTOS
```

Talk from the RTOS: type `!talk` in the channel chat (TeamSpeak desktop, or `ts3cli --say '!talk'`). The bot
Opus-encodes `clip.raw` (16 kHz mono s16le, the stand-in for a mic) on the RTOS and transmits it, half duplex.

Measured on the emulated Cortex-M3: self-test 715 ms, RSA puzzle 220 ms, voice round-trip 150/150 frames at 440.0 Hz,
Opus encode (fixed-point, complexity 3, 16 kHz) ~2 ms per 20 ms frame. QEMU timing is not real-silicon timing.
Firmware: 194 KB text, 81 KB client object (in `.data` because of default member values, so it also costs that much flash).

## Ports

The core needs only `core/port.h`. [docs/PORT_CONTRACT.md](docs/PORT_CONTRACT.md) specifies its eight functions; `test/port_conformance.cpp` checks an implementation. Select a port in CMake with `-DTS3_PORT=pc|freertos|quecopen|stub` (default `pc`; the cross ports build only their library).

| Port | Target | Transport | Status |
|---|---|---|---|
| `pc` (`ports/pc/`) | Linux, Windows (MinGW) | BSD sockets / Winsock | tested: selftest, conformance, `ts3cli` against `teamspeak:latest` |
| `freertos` (`ports/freertos-lwip/`) | QEMU `mps2-an385`, FreeRTOS+TCP | FreeRTOS+TCP sockets | tested in QEMU only; `port_random` is the demo LCG and violates the contract |
| `quecopen` (`ports/quecopen-ec600m/`) | Quectel EC600M | lwIP sockets (QuecOpen SDK) | builds and links; not run on hardware |
| `stub` (`ports/example/`) | none | none | template |

No other RTOS or chip has been ported. Per-port details and deviations: [ports/README.md](ports/README.md).

## Porting to a device

See [docs/PORTING.md](docs/PORTING.md) for the full checklist and a stub port in `ports/example/`. Short version:

1. Implement `core/port.h` per the [contract](docs/PORT_CONTRACT.md) (lwIP socket + `select`/`SO_RCVTIMEO`, RTOS tick, hardware TRNG, RTC/NTP time) and pass `test/port_conformance.cpp`.
2. Build `core/crypto.cpp`, `protocols/ts3/ts3.cpp`, `third_party/ed25519/src/{fe,ge,sc}.c`, mbedTLS 3.6 (needs `CMAC_C`, `CIPHER_MODE_CTR`,
   `ECDSA_C`, `ECP_DP_SECP256R1`, `SHA1/256/512`, `BASE64_C`), libopus (fixed-point).
   Compile with `-fno-exceptions -fno-rtti`.
3. Create the identity once on a PC (`ts3cli` writes `identity.txt`: private key hex + hashcash offset), flash it.
4. One task: `client.poll(10)`; every 20 ms while PTT is held, encode 960 samples and `send_voice()`;
   on release `send_voice(nullptr, 0)`. Decode `on_voice` frames to the speaker.

RAM: `Ts3Client` ≈ 81 KB (mostly `kFragMax`/`kCmdMax` reassembly buffers) + 16 KB QuickLZ table + Opus state.

Gotcha found in QEMU: newlib-nano `printf` has no `%llu`. Use `ts3c::u64_dec()` for 64-bit numbers.

## Known shortcuts (grep `ponytail:`)

- Voice sent unencrypted (server default). Channels with forced voice encryption need `REAL` crypt for voice.
- Variable-time Ed25519 scalar mult; license expiry not checked; custom license `root` ignored.
- Outgoing commands are split but never compressed.
- Client version string/sign is the one ts3j uses (`3.?.? [Build: 5680278000]`); servers that block it need another from `Versions.csv`.

## License

MIT, see `LICENSE`. Third-party components and their licenses: `THIRD_PARTY_LICENSES.md`. Contributions: `CONTRIBUTING.md`.

## Credits

- [ReSpeak](https://github.com/ReSpeak): `tsdeclarations` (protocol spec), `tsclientlib` (reference implementation and the test vectors in `test/selftest.cpp`), `quicklz` (decompressor ported into `core/crypto.cpp`). MIT or Apache-2.0.
- [Orson Peters](https://github.com/orlp/ed25519): Ed25519 group math. zlib.
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (Apache-2.0 or GPL-2.0-or-later) and [Opus](https://github.com/xiph/opus) (BSD-3-Clause).
- [FreeRTOS](https://github.com/FreeRTOS/FreeRTOS) and FreeRTOS-Plus-TCP (MIT): kernel, TCP/IP stack and the QEMU `mps2-an385` demo the RTOS port builds on.
- [Quectel](https://developer.quectel.com): QuecOpen C-SDK and QuecPython, used for the EC600M targets.
- The `teamspeak` Docker image is the server the client was tested against. TeamSpeak is a trademark of TeamSpeak Systems GmbH; this project is independent.

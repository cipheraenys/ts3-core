# Port: Quectel EC600M (QuecOpen OCPU)

Implements `src/port.h` for EC600M-class modules (ASR1606, Cortex-R4, ThreadX) with the QuecOpen C-SDK. Contract: [docs/PORT_CONTRACT.md](../../docs/PORT_CONTRACT.md).

| `port.h` | Implemented with |
|---|---|
| `port_udp_*` | SDK lwIP BSD sockets (`sockets.h`): `socket`, `connect`, `send`, `select` + `recv`. Numeric IPv4 via `inet_pton`, names via `gethostbyname`. A data call must be up first (`ql_data_call.h`). |
| `port_millis` | `ql_rtos_get_systicks()` divided by `QL_TICKS_PER_MS` |
| `port_unix_time` | `ql_rtc_get_time()`, converted to epoch seconds |
| `port_random` | `ql_geu_random_number()` (the GEU engine) |
| `port_log` | `vsnprintf` into 256 bytes, then `printf` |

Files: `port_quecopen.cpp` (the port), `app_quecopen.cpp` (PTT + audio + Opus loop), `sdk-integration/` (staging and SDK makefile), `BUILD.md` (full build steps).

## Status

- Builds and links into `app.bin` against SDK `LTE01R06A01_AIRTC_C_SDK_A_BETA0212` (per `BUILD.md`).
- Not run on hardware. Nothing in this directory has been executed on an EC600M, and `test/port_conformance.cpp` has not been run on this port.

## Known gaps against the contract

- `QL_TICKS_PER_MS` defaults to 1. The SDK exposes no tick rate; measure it on the device (see the comment in `port_quecopen.cpp`). A wrong value breaks every timer in the core.
- `port_unix_time` returns 0 when `ql_rtc_get_time` fails (RTC not yet set from the network). The contract asks for a best-effort fallback instead, and `port_conformance` fails on 0.
- `port_random`: whether `ql_geu_random_number()` is a true entropy source is unconfirmed (`BUILD.md`, "Not yet verified"). Confirm with Quectel documentation or the chip manual before shipping.

## Build

- Full firmware: `BUILD.md`.
- Library only, for a syntax/link check from CMake: `cmake -S . -B build -DTS3_PORT=quecopen -DCMAKE_TOOLCHAIN_FILE=<arm-none-eabi toolchain> -DTS3_PORT_INCLUDE_DIRS="<sdk>/common/include;<sdk>/common/include/lwipv4v6"`. This produces `libts3c_port.a` only and was not run against the SDK.

## Run the conformance test on the module

Link `test/port_conformance.cpp` into the app, call `port_conformance_run(host, 7777, nullptr)` from the app task after the data call is up, and run a UDP echo server on `host` (snippet in the contract). Results appear through `port_log`.

# Third-party licenses

The project's own code (`src/`, `pc/`, `test/`, `rtos/`, `ports/`) is MIT, see `LICENSE`.
Third-party works below keep their own licenses and copyrights; nothing here claims ownership of them.

| Component | Where | License | Notes |
|---|---|---|---|
| orlp/ed25519 | `third_party/ed25519/` (vendored; `LICENSE.txt`, `VERSION`) | zlib | Copyright (c) 2015 Orson Peters. Unmodified group math only. |
| Mbed TLS 3.6 | external, linked | Apache-2.0 (or GPL-2.0-or-later, dual) | Not vendored. Install via package manager or build separately. |
| Opus (libopus) | external, linked by `pc/` and the RTOS demo | BSD-3-Clause | Not vendored. |
| tsclientlib / tsdeclarations (ReSpeak) | protocol reference; test vectors in `test/selftest.cpp`; QuickLZ decompressor in `src/crypto.cpp` is a port of ReSpeak/quicklz | MIT or Apache-2.0 | Used as specification and for test vectors. Upstream: https://github.com/ReSpeak |
| FreeRTOS, FreeRTOS-Plus-TCP | external, only for `rtos/freertos_qemu` | MIT | Not vendored; fetched into `~/ts3deps`. |

"TeamSpeak" is a trademark of TeamSpeak Systems GmbH. This project is independent and not affiliated with or endorsed by it.

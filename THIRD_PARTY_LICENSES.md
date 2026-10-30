# Third-party licenses

The project's own code (`core/`, `protocols/`, `ports/`, `test/`, `docs/`) is MIT, see `LICENSE`.
Third-party works below keep their own licenses and copyrights.
The submodules are not copied into this repository: each is pinned to the commit or tag shown and carries its own license file.

| Component | Where | License | Notes |
|---|---|---|---|
| orlp/ed25519 | `third_party/ed25519/` (git submodule, `b1f19fa`; `license.txt`) | zlib | Copyright (c) 2015 Orson Peters. Only `src/{fe,ge,sc}.c` and headers are built, unmodified. Full text below. |
| Mbed TLS 3.6 | `third_party/mbedtls/` (git submodule, `v3.6.6`) | Apache-2.0 (or GPL-2.0-or-later, dual) | Built from source by the RTOS/QuecOpen ports; the PC build links a system install. |
| Opus (libopus) | `third_party/opus/` (git submodule, `v1.5.2`) | BSD-3-Clause | Built from source by the RTOS/QuecOpen ports; the PC build links a system install. |
| tsclientlib / tsdeclarations (ReSpeak) | protocol reference; test vectors in `test/selftest.cpp`; QuickLZ decompressor in `core/crypto.cpp` is a port of ReSpeak/quicklz | MIT or Apache-2.0 | Used as specification and for test vectors. Upstream: https://github.com/ReSpeak |
| FreeRTOS, FreeRTOS-Plus-TCP | external, only for `ports/freertos-lwip` | MIT | Not vendored; fetched into `~/ts3deps`. |
| QuecOpen C-SDK | vendor download, not in this repository | Quectel terms | Used only by `ports/quecopen-ec600m`. |

## orlp/ed25519 (zlib)

Copyright (c) 2015 Orson Peters <orsonpeters@gmail.com>

This software is provided 'as-is', without any express or implied warranty. In no event will the
authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial
applications, and to alter it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the
   original software. If you use this software in a product, an acknowledgment in the product
   documentation would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be misrepresented as
   being the original software.

3. This notice may not be removed or altered from any source distribution.

"TeamSpeak" is a trademark of TeamSpeak Systems GmbH. This project is independent and not affiliated with or endorsed by it.

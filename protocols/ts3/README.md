# protocols/ts3

TeamSpeak 3 client protocol on top of `core/`: Init1 handshake with RSA puzzle, `initivexpand2`/`clientek`, `clientinit`, channel list (fragmented, QuickLZ), ping/pong, resend, voice packets (Opus in, Opus out; the codec is not included), clean disconnect.

- API: `ts3.h` (`Ts3Client`, `Ts3Config`). Single-threaded: call `poll()` and `send_*()` from one task.
- Depends on `core/crypto.h` and, through it, `core/port.h`. No platform headers.
- Spec and reference: ReSpeak `tsdeclarations` and `tsclientlib`. Test vectors: `test/selftest.cpp`.
- Build with `-I core -I protocols/ts3` and `#include "ts3.h"`.

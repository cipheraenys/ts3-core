# Platforms

`core/` is portable C++ and depends only on mbedTLS and ed25519. A target implements the eight functions in `core/port.h` ([contract](PORT_CONTRACT.md)) and its own app (audio, PTT).

TS3 runs over UDP without TLS. RAM: `Ts3Client` ≈ 81 KB (reassembly buffers), 16 KB QuickLZ table, Opus state, the mbedTLS ECP working set, socket buffers.

Opus runs in the app task, fixed-point, with `VAR_ARRAYS` (stack-allocated). Stacks: QEMU demo 64 KB, EC600M app 32 KiB (both unmeasured).

| Device | Core | RAM | Status |
|---|---|---|---|
| EC600M / ASR1606 (BQ280) | Cortex-R4 | unknown; link reports bss ≈ 105 KB | target; builds, not run on hardware |

No other device has been ported. Per-port status: [ports/README.md](../ports/README.md). Porting steps: [PORTING.md](PORTING.md).

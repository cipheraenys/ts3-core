# Building TS3-RTOS for Quectel EC600M (QuecOpen OCPU, ASR1606 / Cortex-R4)

A flashable `app.bin` has been built and linked from this repo's sources against the EC600M
QuecOpen C-SDK (`LTE01R06A01_AIRTC_C_SDK_A_BETA0212`). It has **not** been run on hardware yet —
see "Not yet verified".

## Target facts (from the SDK, GCC build on Linux)

| Thing | Value |
|---|---|
| Core | ARM Cortex-R4 (ARMv7-R), soft-float, Thumb |
| Compiler | `arm-none-eabi-g++` 9.2.1 (bundled in `ql-cross-tool/Linux`) |
| Target flags | `-mcpu=cortex-r4 -mfloat-abi=soft -mthumb -mthumb-interwork -mlong-calls -Os` |
| C++ standard | the SDK's default `-std=c++11` is enough; our code needs no C++17 |
| libc | newlib-nano → no `%llu`; `ts3c::u64_dec` already covers it |
| Link | `-T config/common/app_linkscript.ld -nostdlib`, app.elf → objcopy → `CRC64` → `app.bin` |
| App entry | `application_init(ts3_entry, "ts3", 32, 0)` at file scope → `.appRegTable` (kernel launches it) |
| Load address | code at `0x8060_0000`, matching the vendor app image |

## mbedTLS / Opus: bundled, not the SDK's

The SDK's `com_mbedtls.lib` is a reduced **2.16** build (no SHA1/SHA512/CMAC and only partial
ECP/ECDSA — our `crypto.cpp` needs all of them), so we build **our own mbedTLS 3.6 and Opus
(fixed-point)** as static libs for Cortex-R4 and drop every SDK lib that pulls in TLS
(`com_mbedtls`, `com_ssl`, `ByteRTCLite`). Only `ql_common_api.lib` is linked — it resolves the
`ql_*` and lwIP socket symbols we call, and nothing it keeps references mbedTLS.

## Build it

Prereqs: WSL with `unzip`, `bzip2`, `make`; the SDK zip; this repo; mbedTLS 3.6 and Opus sources
(the repo's FreeRTOS deps at `~/ts3deps` are reused). Then:

```sh
# 1. extract the full SDK + the bundled Linux toolchain (ext4, for symlinks)
unzip -q SDK.zip -d ~/ec600m/sdk
tar -xjf ~/ec600m/sdk/ql-sdk/ql-cross-tool/Linux/gcc-arm-none-eabi-9-*.tar.bz2 \
    -C ~/ec600m/sdk/ql-sdk/ql-cross-tool/Linux
chmod +x ~/ec600m/sdk/ql-sdk/ql-cross-tool/Linux/CRC64 \
         ~/ec600m/sdk/ql-sdk/ql-cross-tool/Linux/x86_64/* \
         ~/ec600m/sdk/ql-sdk/ql-cross-tool/Linux/*.sh

# 2. stage this repo's sources + build the bundled libs into interface/ts3/
ports/quecopen/sdk-integration/stage.sh ~/ec600m/sdk/ql-sdk/ql-application/threadx

# 3. build only our interface, linking only ql_common_api (override on the command line —
#    the SDK makefiles are left untouched)
cd ~/ec600m/sdk/ql-sdk/ql-application/threadx
make PLATFORM=linux COMMPILE_DIRS="interface/ts3" DEFAULT_LIBS="common/lib/ql_common_api.lib"
# -> build/app.bin, and a flashable target/<version>/<version>.zip
```

### Integration details that matter (learned the hard way)

- The interface Makefile MUST set `MAKEFILE_PATH` before `include`ing `config/common/makefile.mk`,
  or it takes the top-level branch and fails with `No rule to make target '…/target_compile_dir'`.
- Put our mbedTLS 3.6 `-I` **before** `common/include` — the SDK ships mbedTLS 2.16 headers under
  `common/include/mbedtls`, which otherwise shadow ours.
- Staged `.cpp` include our headers flat (`"port.h"`), not `"../../src/port.h"`; `stage.sh` rewrites this.
- DNS: the SDK's lwIP exports `gethostbyname` / `getaddrinfo_with_pcid`, not plain `getaddrinfo`.
  `port_quecopen.cpp` uses `gethostbyname`.
- The bundled `CRC64` and `arelease.sh` come out of the zip without the execute bit — `chmod +x` them.

## Flashing

`app.bin` is the app partition; flash it (or the generated firmware zip) with `adownload` (ASR flash
tool). This overwrites the vendor PoC app — the dump in `hw/` is the restore path.

## Not yet verified (runtime on silicon)

- Boots and the kernel actually calls `ts3_entry`.
- RAM fit at runtime (link reports bss ≈ 105 KB + heap for Opus/mbedTLS/buffers).
- Audio (`ql_audio_*`), PTT (`ql_pwrkey_*`), and the data call bring-up work as wired.
- `QL_TICKS_PER_MS` (measure on silicon), `ql_geu_random_number` is a true TRNG, and the deploy
  params in `app_quecopen.cpp` (`TS3_HOST`, `APN_NAME`, `PROFILE_IDX`, volume, PTT source).

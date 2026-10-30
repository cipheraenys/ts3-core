#!/bin/sh
# Stage ts3-core into a QuecOpen SDK as interface/ts3/ and build the bundled Opus + mbedTLS 3.6
# static libs for Cortex-R4. After this, build from the SDK's threadx dir (see BUILD.md):
#   make PLATFORM=linux COMMPILE_DIRS="interface/ts3" DEFAULT_LIBS="common/lib/ql_common_api.lib"
#
# Usage: stage.sh <SDK>/ql-application/threadx [<mbedtls36_dir>] [<opus_dir>]
#   mbedtls/opus default to the repo submodules: git submodule update --init third_party/mbedtls third_party/opus third_party/ed25519
set -e
SDK="${1:?need <SDK>/ql-application/threadx}"
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
MB="${2:-$REPO/third_party/mbedtls}"
OPUS="${3:-$REPO/third_party/opus}"
D="$SDK/interface/ts3"

TC="$SDK/../../ql-cross-tool/Linux/gcc-arm-none-eabi-9-2019-q4-major/bin"
GCC="$TC/arm-none-eabi-gcc"; AR="$TC/arm-none-eabi-ar"
ARCH="-mcpu=cortex-r4 -mfloat-abi=soft -mthumb -mthumb-interwork -mlong-calls -Os -g \
      -ffunction-sections -fdata-sections --specs=nano.specs -D__OCPU_COMPILER_GCC__"

rm -rf "$D"; mkdir -p "$D/.obj_mb" "$D/.obj_op"

# sources, staged flat (the sources include "port.h", "ts3.h", ... without paths)
ED="$REPO/third_party/ed25519/src"
cp "$REPO"/core/crypto.cpp "$REPO"/core/*.h "$REPO"/protocols/ts3/ts3.cpp "$REPO"/protocols/ts3/ts3.h "$D"/
cp "$REPO"/ports/quecopen-ec600m/port_quecopen.cpp "$REPO"/ports/quecopen-ec600m/app_quecopen.cpp "$D"/
cp "$ED"/fe.c "$ED"/ge.c "$ED"/sc.c "$ED"/fe.h "$ED"/ge.h "$ED"/sc.h "$ED"/fixedint.h "$ED"/precomp_data.h "$D"/
cp "$REPO"/ports/freertos-lwip/ts3_mbedtls_config.h "$D"/
cp "$REPO"/ports/quecopen-ec600m/sdk-integration/Makefile "$D/Makefile"

# local header copies keep the staged interface self-contained
cp -r "$MB/include" "$D/inc_mbedtls"
cp -r "$OPUS/include" "$D/inc_opus"

# mbedTLS 3.6 -> libmbedcrypto.a (same config the app compiles crypto.cpp with)
CFG="-DMBEDTLS_CONFIG_FILE=\"ts3_mbedtls_config.h\" -I$D"
for f in "$MB"/library/*.c; do
    "$GCC" -c $ARCH -I"$MB/include" $CFG "$f" -o "$D/.obj_mb/$(basename "$f" .c).o"
done
"$AR" rcs "$D/libmbedcrypto.a" "$D"/.obj_mb/*.o

# Opus (fixed-point) -> libopus.a. Source list comes from opus' own .mk files, read with make
# (they are Makefile fragments, not shell).
cat > "$D/.opusget.mk" <<'MK'
include celt_sources.mk
include silk_sources.mk
include opus_sources.mk
print:
	@echo $(CELT_SOURCES) $(SILK_SOURCES) $(SILK_SOURCES_FIXED) $(OPUS_SOURCES)
MK
OF="$ARCH -DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API -DVAR_ARRAYS \
    -I$OPUS -I$OPUS/include -I$OPUS/celt -I$OPUS/silk -I$OPUS/silk/fixed"
for f in $(cd "$OPUS" && make -s -f "$D/.opusget.mk" print); do
    "$GCC" -c $OF "$OPUS/$f" -o "$D/.obj_op/$(echo "$f" | tr / _).o"
done
"$AR" rcs "$D/libopus.a" "$D"/.obj_op/*.o

rm -rf "$D/.obj_mb" "$D/.obj_op" "$D/.opusget.mk"
echo "staged + built libs into $D"
echo "build: cd $SDK && make PLATFORM=linux COMMPILE_DIRS=\"interface/ts3\" DEFAULT_LIBS=\"common/lib/ql_common_api.lib\""

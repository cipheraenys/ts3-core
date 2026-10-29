# interface/ts3/Makefile — TS3-RTOS client as a QuecOpen OCPU app (EC600M / ASR1606 / Cortex-R4).
#
# stage.sh writes this into <SDK>/ql-application/threadx/interface/ts3/ next to the staged sources
# and the bundled libopus.a / libmbedcrypto.a it builds. Then, from the threadx dir:
#   make PLATFORM=linux COMMPILE_DIRS="interface/ts3" DEFAULT_LIBS="common/lib/ql_common_api.lib"
# The command-line overrides keep the SDK's own makefiles untouched (no interface/mbedtls, no
# TLS/RTC libs — see BUILD.md for why only ql_common_api is linked).

TOP_DIR:=$(TOP_DIR)
ROOT_DIR:=$(TOP_DIR)/../..
# Required before the include, or makefile.mk takes its top-level branch instead of the package one.
MAKEFILE_PATH :=$(subst $(strip \),/,$(abspath $(lastword $(MAKEFILE_LIST))))
THIS_DIR:=$(dir $(MAKEFILE_PATH))

SRC_FILES+= crypto.cpp ts3.cpp port_quecopen.cpp app_quecopen.cpp fe.c ge.c sc.c

# Our mbedTLS 3.6 and Opus headers come first: the SDK ships mbedTLS 2.16 under common/include,
# which would otherwise shadow ours.
INC_DIRS+= -I$(THIS_DIR)inc_mbedtls -I$(THIS_DIR)inc_opus -I$(THIS_DIR) \
           -I${TOP_DIR}/ -I${TOP_DIR}/common/include -I${TOP_DIR}/common/include/lwipv4v6

# crypto.cpp must use the SAME mbedTLS config as libmbedcrypto.a was built with, or the two
# disagree on struct layout at runtime. (Harmless for the ed25519 .c, which pull in no mbedTLS.)
U_FLAGS:= -DMBEDTLS_CONFIG_FILE='"ts3_mbedtls_config.h"'

U_LIBS:= $(THIS_DIR)libopus.a $(THIS_DIR)libmbedcrypto.a

include ${TOP_DIR}/config/common/makefile.mk

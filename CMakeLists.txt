# Optional, portable build. build.sh and ports/freertos-lwip/Makefile remain supported and independent.
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
# Port selection: -DTS3_PORT=pc|freertos|quecopen|stub (default pc). See ports/README.md.
# Needs: git submodule update --init third_party/ed25519
cmake_minimum_required(VERSION 3.13)
project(ts3c LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TS3C_BUILD_CLI "Build ts3cli (TS3_PORT=pc only; needs libopus)" ON)
option(TS3C_BUILD_TESTS "Build selftest and port_conformance (TS3_PORT=pc or stub)" ON)

set(TS3_PORT pc CACHE STRING "Port implementing core/port.h: pc, freertos, quecopen or stub")
set_property(CACHE TS3_PORT PROPERTY STRINGS pc freertos quecopen stub)
# freertos/quecopen need the RTOS/SDK headers and a cross toolchain (-DCMAKE_TOOLCHAIN_FILE=...).
set(TS3_PORT_INCLUDE_DIRS "" CACHE STRING "Extra include dirs for the port source, ';'-separated")
set(TS3_PORT_DEFINITIONS "" CACHE STRING "Extra compile definitions for the port source, ';'-separated")

if(TS3_PORT STREQUAL "pc")
  set(TS3_PORT_SOURCE ports/pc/port_pc.cpp)
elseif(TS3_PORT STREQUAL "freertos")
  set(TS3_PORT_SOURCE ports/freertos-lwip/port_freertos.cpp)
  string(TIMESTAMP TS3_BUILD_EPOCH "%s" UTC)  # port_unix_time() fallback when the board has no RTC
  list(APPEND TS3_PORT_DEFINITIONS TS3_BUILD_EPOCH=${TS3_BUILD_EPOCH}u)
elseif(TS3_PORT STREQUAL "quecopen")
  set(TS3_PORT_SOURCE ports/quecopen-ec600m/port_quecopen.cpp)
elseif(TS3_PORT STREQUAL "stub")
  set(TS3_PORT_SOURCE ports/example/port_stub.cpp)
else()
  message(FATAL_ERROR "TS3_PORT must be pc, freertos, quecopen or stub (got '${TS3_PORT}')")
endif()

# Cross ports produce libraries only: the firmware app and its main() belong to the target build.
if(NOT TS3_PORT STREQUAL "pc")
  set(TS3C_BUILD_CLI OFF)
endif()
if(NOT (TS3_PORT STREQUAL "pc" OR TS3_PORT STREQUAL "stub"))
  set(TS3C_BUILD_TESTS OFF)
endif()

# ---- dependencies: mbedTLS 3.x ----
find_package(MbedTLS CONFIG QUIET)
if(TARGET MbedTLS::mbedcrypto)
  set(TS3C_MBEDCRYPTO MbedTLS::mbedcrypto)
else()
  find_library(MBEDCRYPTO_LIBRARY NAMES mbedcrypto)
  find_path(MBEDTLS_INCLUDE_DIR NAMES mbedtls/build_info.h)  # only exists in mbedTLS 3.x
  if(NOT MBEDCRYPTO_LIBRARY OR NOT MBEDTLS_INCLUDE_DIR)
    message(FATAL_ERROR "mbedTLS 3.x (libmbedcrypto + mbedtls/build_info.h) not found. "
                        "Set -DMBEDTLS_INCLUDE_DIR=... -DMBEDCRYPTO_LIBRARY=... or CMAKE_PREFIX_PATH.")
  endif()
  add_library(ts3c_mbedcrypto UNKNOWN IMPORTED)
  set_target_properties(ts3c_mbedcrypto PROPERTIES IMPORTED_LOCATION "${MBEDCRYPTO_LIBRARY}"
                        INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIR}")
  set(TS3C_MBEDCRYPTO ts3c_mbedcrypto)
endif()

# ---- ed25519: git submodule (orlp/ed25519) ----
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ed25519/src/ge.c)
  message(FATAL_ERROR "third_party/ed25519 is empty. Run: git submodule update --init third_party/ed25519")
endif()
add_library(ed25519 STATIC third_party/ed25519/src/fe.c third_party/ed25519/src/ge.c third_party/ed25519/src/sc.c)
target_include_directories(ed25519 PUBLIC third_party/ed25519/src)

# ---- core: protocol-agnostic crypto + the port.h seam ----
add_library(ts3c_core STATIC core/crypto.cpp)
target_include_directories(ts3c_core PUBLIC core)
target_link_libraries(ts3c_core PUBLIC ed25519 ${TS3C_MBEDCRYPTO})

# ---- protocol: TeamSpeak 3 ----
add_library(ts3c STATIC protocols/ts3/ts3.cpp)
target_include_directories(ts3c PUBLIC protocols/ts3)
target_link_libraries(ts3c PUBLIC ts3c_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ts3c_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
  target_compile_options(ts3c PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---- port: implements core/port.h, one per target (the core never includes platform headers) ----
add_library(ts3c_port STATIC ${TS3_PORT_SOURCE})
target_include_directories(ts3c_port PUBLIC core PRIVATE ${TS3_PORT_INCLUDE_DIRS})
target_compile_definitions(ts3c_port PRIVATE ${TS3_PORT_DEFINITIONS})
if(TS3_PORT STREQUAL "pc" AND WIN32)
  target_link_libraries(ts3c_port PUBLIC ws2_32)
endif()

if(TS3C_BUILD_TESTS)
  enable_testing()
  add_executable(selftest test/selftest.cpp)
  target_link_libraries(selftest PRIVATE ts3c_core ts3c_port)  # port after core: core calls port_*
  add_test(NAME selftest COMMAND selftest)

  # Needs only port.h. With TS3_PORT=stub it fails on purpose until the stub is implemented.
  add_executable(port_conformance test/port_conformance.cpp test/port_conformance_pc.cpp)
  target_link_libraries(port_conformance PRIVATE ts3c_port)
  add_test(NAME port_conformance COMMAND port_conformance)
endif()

if(TS3C_BUILD_CLI)
  find_library(OPUS_LIBRARY NAMES opus)
  find_path(OPUS_INCLUDE_DIR NAMES opus/opus.h)  # main.cpp includes <opus/opus.h>
  if(NOT OPUS_LIBRARY OR NOT OPUS_INCLUDE_DIR)
    message(FATAL_ERROR "libopus not found. Install it, set -DOPUS_LIBRARY/-DOPUS_INCLUDE_DIR, or -DTS3C_BUILD_CLI=OFF.")
  endif()
  add_executable(ts3cli ports/pc/main.cpp)
  target_include_directories(ts3cli PRIVATE "${OPUS_INCLUDE_DIR}")
  target_link_libraries(ts3cli PRIVATE ts3c ts3c_port "${OPUS_LIBRARY}")
endif()

#!/bin/sh
# PC build (MSYS2 ucrt64 or Linux). Needs mbedTLS 3.6 and libopus.
set -e
cd "$(dirname "$0")"
mkdir -p out
CXX="${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Ithird_party/ed25519"
CC="${CC:-gcc} -O2 -Ithird_party/ed25519"
for f in fe ge sc; do $CC -c third_party/ed25519/$f.c -o out/$f.o; done
ED="out/fe.o out/ge.o out/sc.o"
LIBS="-lmbedcrypto"
case "$(uname -s)" in MINGW*|MSYS*) NET="-lws2_32";; *) NET="";; esac
$CXX test/selftest.cpp src/crypto.cpp pc/port_pc.cpp $ED $LIBS $NET -o out/selftest
$CXX pc/main.cpp src/ts3.cpp src/crypto.cpp pc/port_pc.cpp $ED $LIBS -lopus $NET -o out/ts3cli
$CXX test/port_conformance.cpp test/port_conformance_pc.cpp pc/port_pc.cpp $NET -o out/port_conformance
echo built: out/selftest out/ts3cli out/port_conformance

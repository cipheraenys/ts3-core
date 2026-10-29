#!/bin/sh
# PC build (MSYS2 ucrt64 or Linux). Needs mbedTLS 3.6, libopus, and `git submodule update --init third_party/ed25519`.
set -e
cd "$(dirname "$0")"
mkdir -p out
ED_SRC=third_party/ed25519/src
CXX="${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Icore -Iprotocols/ts3 -I$ED_SRC"
CC="${CC:-gcc} -O2 -I$ED_SRC"
for f in fe ge sc; do $CC -c $ED_SRC/$f.c -o out/$f.o; done
ED="out/fe.o out/ge.o out/sc.o"
LIBS="-lmbedcrypto"
case "$(uname -s)" in MINGW*|MSYS*) NET="-lws2_32";; *) NET="";; esac
$CXX test/selftest.cpp core/crypto.cpp ports/pc/port_pc.cpp $ED $LIBS $NET -o out/selftest
$CXX ports/pc/main.cpp protocols/ts3/ts3.cpp core/crypto.cpp ports/pc/port_pc.cpp $ED $LIBS -lopus $NET -o out/ts3cli
$CXX test/port_conformance.cpp test/port_conformance_pc.cpp ports/pc/port_pc.cpp $NET -o out/port_conformance
echo built: out/selftest out/ts3cli out/port_conformance

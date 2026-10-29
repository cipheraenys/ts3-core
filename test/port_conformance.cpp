// Conformance checks for an implementation of src/port.h. See docs/PORT_CONTRACT.md.
// Copyright (c) 2026 cipheraenys@gmail.com, MIT license.
//
// Depends only on port.h, so it links against any port. Output goes through port_log, not stdio,
// because some targets have no stdout until the port works.
#include "port_conformance.h"

#include <cstddef>
#include <cstring>

#include "../src/port.h"

static int g_checks, g_fail;

#define CHECK(c)                                                         \
    do {                                                                 \
        g_checks++;                                                      \
        if (!(c)) {                                                      \
            g_fail++;                                                    \
            port_log("FAIL line %d: %s", __LINE__, #c);                  \
        }                                                                \
    } while (0)

// Static: keeps RTOS task stacks small.
static uint8_t g_rnd[4096];
static uint8_t g_pkt[512], g_rx[600];

static int32_t since(uint32_t t0) { return (int32_t)(port_millis() - t0); }

static void test_open_close(const char *host, uint16_t port) {
    // Repeated cycles catch a port that leaks handles (the FreeRTOS port has 4 slots).
    for (int i = 0; i < 8; i++) {
        int s = port_udp_open(host, port);
        CHECK(s >= 0);
        if (s >= 0) port_udp_close(s);
    }
    // ".invalid" never resolves (RFC 6761 6.4). A resolver that hijacks NXDOMAIN fails this check.
    int bad = port_udp_open("ts3c-conformance.invalid", 9);
    CHECK(bad < 0);
    if (bad >= 0) port_udp_close(bad);
}

static void test_timeout_and_clock(const char *host, uint16_t port) {
    int s = port_udp_open(host, port);
    CHECK(s >= 0);
    if (s < 0) return;

    uint32_t u0 = port_unix_time(), m0 = port_millis();
    int r = port_udp_recv(s, g_rx, sizeof g_rx, 100);
    int32_t dt = since(m0);
    CHECK(r == 0);
    // Lower bound is loose: select() and RTOS ticks may return a few ms early.
    CHECK(dt >= 80);
    CHECK(dt < 1000);

    // timeout 0 is a poll; the core drains its socket with it.
    m0 = port_millis();
    r = port_udp_recv(s, g_rx, sizeof g_rx, 0);
    CHECK(r == 0);
    CHECK(since(m0) < 50);

    uint32_t u1 = port_unix_time();
    CHECK(u1 >= u0 && u1 - u0 <= 2);
    port_udp_close(s);
}

static void test_monotonic() {
    uint32_t prev = port_millis();
    int back = 0;
    for (int i = 0; i < 200000; i++) {
        uint32_t now = port_millis();
        if ((int32_t)(now - prev) < 0) back++;
        prev = now;
    }
    CHECK(back == 0);
}

static void test_unix_time() {
    // 2025-01-01T00:00:00Z. Below this the clock is unset (RTC reset value, NITZ not yet received).
    CHECK(port_unix_time() >= 1735689600u);
}

static bool echo_once(int s, size_t len, void (*service)(void)) {
    if (port_udp_send(s, g_pkt, len) != (int)len) return false;
    if (service) service();
    memset(g_rx, 0xEE, sizeof g_rx);
    int r = port_udp_recv(s, g_rx, sizeof g_rx, 2000);
    return r == (int)len && memcmp(g_rx, g_pkt, len) == 0;
}

// Up to 3 attempts: stacks may drop the first datagram while they resolve the peer's MAC address
// (seen with FreeRTOS+TCP and ARP). The core retransmits, so this is allowed; losing every attempt is not.
static bool echo_one(int s, size_t len, void (*service)(void), uint8_t seed) {
    for (size_t i = 0; i < len; i++) g_pkt[i] = (uint8_t)(seed + i * 7);
    for (int attempt = 0; attempt < 3; attempt++)
        if (echo_once(s, len, service)) return true;
    return false;
}

static void test_echo(const char *host, uint16_t port, void (*service)(void)) {
    int s = port_udp_open(host, port);
    CHECK(s >= 0);
    if (s < 0) return;
    // 500 is the core's largest datagram (MAX_PKT in src/ts3.cpp).
    CHECK(echo_one(s, 1, service, 1));
    CHECK(echo_one(s, 100, service, 2));
    CHECK(echo_one(s, 500, service, 3));

    // Datagram boundaries must survive: two sends give two recvs of the original sizes.
    // Retried like echo_one: a stack that is resolving the peer's MAC may drop the second send.
    memset(g_pkt, 0x5A, 20);
    bool split = false;
    for (int attempt = 0; attempt < 3 && !split; attempt++) {
        port_udp_send(s, g_pkt, 10);
        port_udp_send(s, g_pkt, 20);
        if (service) service(), service();
        int a = port_udp_recv(s, g_rx, sizeof g_rx, 2000);
        int b = port_udp_recv(s, g_rx, sizeof g_rx, a > 0 ? 2000 : 0);
        split = (a == 10 && b == 20) || (a == 20 && b == 10);
        if (!split) {
            port_log("boundary attempt %d: recv sizes %d %d, want 10 and 20", attempt + 1, a, b);
            while (port_udp_recv(s, g_rx, sizeof g_rx, 200) > 0) {}
        }
    }
    CHECK(split);

    // After the last datagram is consumed the socket must report timeout, not stale data.
    CHECK(port_udp_recv(s, g_rx, sizeof g_rx, 50) == 0);
    port_udp_close(s);
}

static void test_random() {
    port_random(g_rnd, sizeof g_rnd);

    // Chi-square over byte values: 4096 samples, 256 bins, expected 16 each. A uniform source gives
    // about 255 +- 23. Bounds are ~4 sigma so a good source fails with probability < 1e-4.
    static uint16_t hist[256];
    memset(hist, 0, sizeof hist);
    for (size_t i = 0; i < sizeof g_rnd; i++) hist[g_rnd[i]]++;
    uint32_t ssq = 0, distinct = 0;
    for (int v = 0; v < 256; v++) {
        int d = (int)hist[v] - 16;
        ssq += (uint32_t)(d * d);
        distinct += hist[v] != 0;
    }
    uint32_t chi = ssq / 16;
    CHECK(chi >= 150 && chi <= 400);
    CHECK(distinct >= 245);

    // Ones fraction: 32768 bits, sigma 0.28%; accept 48%..52%.
    uint32_t ones = 0;
    for (size_t i = 0; i < sizeof g_rnd; i++)
        for (uint8_t b = g_rnd[i]; b; b &= (uint8_t)(b - 1)) ones++;
    CHECK(ones > 32768u * 48 / 100 && ones < 32768u * 52 / 100);

    // Adjacent equal bytes: expected 16, sigma 4.
    uint32_t same = 0;
    for (size_t i = 1; i < sizeof g_rnd; i++) same += g_rnd[i] == g_rnd[i - 1];
    CHECK(same < 64);

    // No repeated 16-byte block: catches generators whose period is shorter than the buffer.
    bool dup = false;
    for (size_t i = 0; i < sizeof g_rnd && !dup; i += 16)
        for (size_t j = i + 16; j < sizeof g_rnd; j += 16)
            if (memcmp(g_rnd + i, g_rnd + j, 16) == 0) { dup = true; break; }
    CHECK(!dup);

    // Back-to-back calls must not repeat output (a reseed-per-call or fixed-seed generator would).
    uint8_t x[32], y[32];
    port_random(x, sizeof x);
    port_random(y, sizeof y);
    CHECK(memcmp(x, y, sizeof x) != 0);

    // Length edge cases: exact length written, unaligned pointer, len 0 writes nothing.
    uint8_t guard[24];
    memset(guard, 0xA5, sizeof guard);
    port_random(guard + 1, 0);
    bool intact = true;
    for (uint8_t c : guard) intact = intact && c == 0xA5;
    CHECK(intact);
    port_random(guard + 5, 3);
    CHECK(guard[4] == 0xA5 && guard[8] == 0xA5);
}

static void test_log() {
    // Cannot observe the output; this checks the varargs path and a long line do not crash.
    char longmsg[400];
    memset(longmsg, 'x', sizeof longmsg - 1);
    longmsg[sizeof longmsg - 1] = 0;
    port_log("conformance: %d %u %x %c %s", -1, 4000000000u, 0xbeefu, 'c', "str");
    port_log("conformance: long line %s", longmsg);
    CHECK(true);
}

int port_conformance_run(const char *echo_host, uint16_t echo_port, void (*service)(void)) {
    g_checks = g_fail = 0;
    test_open_close(echo_host, echo_port);
    test_timeout_and_clock(echo_host, echo_port);
    test_monotonic();
    test_unix_time();
    test_echo(echo_host, echo_port, service);
    test_random();
    test_log();
    port_log("port_conformance: %d checks, %d failed", g_checks, g_fail);
    return g_fail;
}

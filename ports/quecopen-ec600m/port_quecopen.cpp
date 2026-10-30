// QuecOpen (OCPU) port of core/port.h for Quectel EC600M-class modules (ASR1606).
//
// Compiles and links against the EC600M QuecOpen C-SDK (LTE01R06A01_AIRTC_C_SDK_A_BETA0212).
// The app must bring up a data call (ql_data_call.h) before port_udp_open.
//
// Build: ports/quecopen-ec600m/BUILD.md.

#include "port.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" {
#include "sockets.h"   // socket/connect/send/recv/select/close
#include "netdb.h"     // gethostbyname
#include "inet.h"      // struct in_addr
#include "inet6.h"     // inet_pton
#include "ql_rtos.h"   // u32 ql_rtos_get_systicks(void)
#include "ql_rtc.h"    // int ql_rtc_get_time(ql_rtc_time_t*)
#include "ql_platform.h"  // unsigned int ql_geu_random_number(void)
}

// ponytail: the SDK exposes no tick rate; measure ticks across ql_rtos_task_sleep_ms(1000) and set QL_TICKS_PER_MS.
#ifndef QL_TICKS_PER_MS
#define QL_TICKS_PER_MS 1u
#endif

int port_udp_open(const char *host, uint16_t port) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    // Numeric IPv4, else gethostbyname (the SDK's lwIP has no plain getaddrinfo). Needs the data call up.
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        struct hostent *he = gethostbyname(host);
        if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) {
            close(s);
            return -1;
        }
        memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof sa.sin_addr);
    }
    if (connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) {  // "connected" UDP
        close(s);
        return -1;
    }
    return s;
}

void port_udp_close(int s) {
    if (s >= 0) close(s);
}

int port_udp_send(int s, const void *buf, size_t len) {
    return (int)send(s, (const char *)buf, len, 0);
}

int port_udp_recv(int s, void *buf, size_t len, uint32_t timeout_ms) {
    struct timeval tv;
    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)(timeout_ms % 1000) * 1000;
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    int n = select(s + 1, &r, nullptr, nullptr, &tv);
    if (n == 0) return 0;   // timeout
    if (n < 0) return -1;
    return (int)recv(s, (char *)buf, len, 0);
}

uint32_t port_millis(void) {
    return (uint32_t)(ql_rtos_get_systicks() / QL_TICKS_PER_MS);
}

uint32_t port_unix_time(void) {
    ql_rtc_time_t tm;
    if (ql_rtc_get_time(&tm) != 0) return 0;  // expected to be NITZ/NTP-synced on the network (unverified)
    // SDK convention: tm_mon in [1,12], tm_year "since 1970". Accept an absolute year too.
    int year = tm.tm_year >= 1900 ? tm.tm_year : 1970 + tm.tm_year;
    int mon = tm.tm_mon;  // 1..12
    // days_from_civil (Howard Hinnant), civil -> days since 1970-01-01
    int y = year - (mon <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + (unsigned)tm.tm_mday - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + (long)doe - 719468;
    return (uint32_t)(days * 86400L + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
}

void port_random(void *buf, size_t len) {
    // Security-critical: all key material comes from here. ql_geu_random_number() (GEU engine) is
    // assumed to be a TRNG; unconfirmed. Never fall back to a PRNG.
    uint8_t *p = (uint8_t *)buf;
    while (len) {
        unsigned int v = ql_geu_random_number();
        size_t n = len < sizeof v ? len : sizeof v;
        memcpy(p, &v, n);
        p += n;
        len -= n;
    }
}

void port_log(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    printf("[ts3] %s\n", line);  // assumes printf reaches the debug UART (unverified); else use ql_uart/QL_LOG
}

// Audio and PTT: app_quecopen.cpp.

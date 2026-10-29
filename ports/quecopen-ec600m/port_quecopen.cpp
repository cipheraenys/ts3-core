// QuecOpen (OCPU) port of core/port.h for Quectel EC600M-class modules (ASR1606 / CraneL).
//
// Signatures verified against the EC600M QuecOpen C-SDK (LTE01R06..AIRTC, BETA0212):
//   ql-sdk/ql-application/threadx/common/include/{ql_rtos,ql_rtc,ql_platform}.h and
//   .../common/include/lwipv4v6/{sockets,netdb,inet}.h
// The app that uses this port must be registered with the SDK's application_init() macro
// (ql_application.h) and must bring up a data call (ql_data_call.h) before port_udp_open,
// so the PDP context / default netif exists for sockets and DNS.
//
// Build: add -I.../common/include and -I.../common/include/lwipv4v6 to the SDK app makefile,
// then compile core/crypto.cpp, protocols/ts3/ts3.cpp, third_party/ed25519/src/*.c, this file, mbedTLS and libopus.
// Only this file is per-chip; protocols/ts3/ts3.cpp, core/crypto.cpp, third_party/ are portable.

#include "port.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" {
#include "sockets.h"   // lwipv4v6: socket/connect/send/recv/select/setsockopt/close, SO_RCVTIMEO
#include "netdb.h"     // lwipv4v6: getaddrinfo
#include "inet.h"      // lwipv4v6: inet_addr
#include "inet6.h"     // lwipv4v6: inet_pton
#include "ql_rtos.h"   // u32 ql_rtos_get_systicks(void)
#include "ql_rtc.h"    // int ql_rtc_get_time(ql_rtc_time_t*)
#include "ql_platform.h"  // unsigned int ql_geu_random_number(void)  (hardware GEU RNG)
}

// ql_rtos_get_systicks() returns RTOS ticks since reset. The SDK exposes no tick-rate macro,
// so this is the one knob to verify on real silicon (ponytail: the physical world needs tuning).
// Measure once: ticks delta across a known ql_rtos_task_sleep_ms(1000) and set this.
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

    // Numeric host: no DNS. Hostname: resolve with gethostbyname. The SDK's lwIP exports
    // gethostbyname and getaddrinfo_with_pcid but NOT plain getaddrinfo; both need the data
    // call up first. gethostbyname uses the default PDP context, so the port stays profile-agnostic.
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {  // 1 = valid numeric IP; else resolve
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
    if (ql_rtc_get_time(&tm) != 0) return 0;  // RTC is NITZ/NTP-synced once on the network
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
    // Security-critical: the TS3 handshake (ephemeral keys, IV) breaks under weak entropy.
    // ql_geu_random_number() is the hardware GEU engine; never fall back to pseudo-RNG here.
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
    printf("[ts3] %s\n", line);  // QuecOpen redirects stdio to the debug UART; swap to the
                                 // SDK log sink (ql_uart/QL_LOG) if your build doesn't wire printf.
}

// ponytail: audio + PTT are app-layer, not part of core/port.h. Verified SDK APIs for the app main:
//   PTT     : ql_get_pwrkey_status(), ql_pwrkey_register_irq(cb)   [ql_power.h]
//   PCM     : ql_pcm_open(...) + read/write                        [ql_audio.h]
//   speaker : ql_bind_speakerpa_cb, ql_set_audio_path_receiver     [ql_audio.h, seen in dump]
// Register the app with application_init(ts3_entry,"ts3",<stack_kib>,<prio>) and feed 20 ms
// frames to Opus, same loop as ports/freertos-lwip/app.cpp.

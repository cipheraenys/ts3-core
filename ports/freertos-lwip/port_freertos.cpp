// port.h on FreeRTOS + FreeRTOS+TCP. Template for real devices: swap time/random for RTC + TRNG.
#include <cstdarg>
#include <cstdio>
#include <ctime>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
}

#include "port.h"

static Socket_t socks[4];
static freertos_sockaddr dest[4];

int port_udp_open(const char *host, uint16_t port) {
    int i = 0;
    while (i < 4 && socks[i]) i++;
    // ponytail: dotted IPv4 only; use FreeRTOS_gethostbyname() when you need DNS names.
    uint32_t ip = FreeRTOS_inet_addr(host);
    if (i == 4 || !ip) return -1;
    Socket_t s = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP);
    if (s == FREERTOS_INVALID_SOCKET) return -1;
    freertos_sockaddr local = {};
    local.sin_len = sizeof local;
    local.sin_family = FREERTOS_AF_INET;  // port 0 = any free port
    if (FreeRTOS_bind(s, &local, sizeof local) != 0) {
        FreeRTOS_closesocket(s);
        return -1;
    }
    dest[i] = {};
    dest[i].sin_len = sizeof dest[i];
    dest[i].sin_family = FREERTOS_AF_INET;
    dest[i].sin_port = FreeRTOS_htons(port);
    dest[i].sin_address.ulIP_IPv4 = ip;
    socks[i] = s;
    return i;
}

void port_udp_close(int i) {
    FreeRTOS_closesocket(socks[i]);
    socks[i] = nullptr;
}

int port_udp_send(int i, const void *buf, size_t len) {
    return (int)FreeRTOS_sendto(socks[i], buf, len, 0, &dest[i], sizeof dest[i]);
}

int port_udp_recv(int i, void *buf, size_t len, uint32_t timeout_ms) {
    TickType_t t = pdMS_TO_TICKS(timeout_ms);
    FreeRTOS_setsockopt(socks[i], 0, FREERTOS_SO_RCVTIMEO, &t, sizeof t);
    freertos_sockaddr from;
    socklen_t fl = sizeof from;
    int32_t r = FreeRTOS_recvfrom(socks[i], buf, len, 0, &from, &fl);
    if (r == -pdFREERTOS_ERRNO_EWOULDBLOCK) return 0;
    return (int)r;
}

uint32_t port_millis(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

// ponytail: no RTC in QEMU mps2 (time() returns uptime), so build time + uptime.
// A radio reads RTC / NTP / cellular NITZ here.
uint32_t port_unix_time(void) { return TS3_BUILD_EPOCH + port_millis() / 1000; }

extern "C" BaseType_t xApplicationGetRandomNumber(uint32_t *n);

// ponytail: the demo's LCG is NOT a secure RNG; wire this to the SoC TRNG on hardware.
void port_random(void *buf, size_t len) {
    uint32_t a, b;
    for (size_t i = 0; i < len; i++) {
        xApplicationGetRandomNumber(&a), xApplicationGetRandomNumber(&b);
        ((uint8_t *)buf)[i] = (uint8_t)(a ^ (b >> 7) ^ xTaskGetTickCount());
    }
}

void port_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\r\n");
}

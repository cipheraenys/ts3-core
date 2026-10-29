// Starting point for a new port: implements core/port.h with neutral values so it compiles and links.
// Replace every body; see docs/PORT_CONTRACT.md and validate with test/port_conformance.cpp.
// Nothing here is functional or secure.
#include <cstdarg>
#include <cstring>

#include "port.h"

int port_udp_open(const char *host, uint16_t port) { (void)host; (void)port; return -1; }  // TODO: socket + connect
void port_udp_close(int s) { (void)s; }
int port_udp_send(int s, const void *buf, size_t len) { (void)s; (void)buf; (void)len; return -1; }
int port_udp_recv(int s, void *buf, size_t len, uint32_t timeout_ms) {
    (void)s; (void)buf; (void)len; (void)timeout_ms;
    return -1;  // TODO: wait up to timeout_ms; bytes read, 0 on timeout, <0 on error
}

uint32_t port_millis(void) { return 0; }      // TODO: RTOS tick / monotonic timer
uint32_t port_unix_time(void) { return 0; }   // TODO: RTC / NTP / cell network time

// TODO: hardware TRNG. Zero-fill is NOT random: with this stub, keys and nonces are predictable.
void port_random(void *buf, size_t len) { memset(buf, 0, len); }

void port_log(const char *fmt, ...) { (void)fmt; }  // TODO: UART/RTT; vsnprintf into a buffer

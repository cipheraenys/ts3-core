// Platform seam: each port implements these (ports/*/port_*.cpp); the core includes no platform headers.
#pragma once
#include <cstddef>
#include <cstdint>

// UDP socket "connected" to host:port. Returns handle >= 0, or < 0 on error.
int port_udp_open(const char *host, uint16_t port);
void port_udp_close(int s);
int port_udp_send(int s, const void *buf, size_t len);
// Wait up to timeout_ms. Returns bytes read, 0 on timeout, < 0 on error.
int port_udp_recv(int s, void *buf, size_t len, uint32_t timeout_ms);

uint32_t port_millis(void);                 // monotonic ms
uint32_t port_unix_time(void);              // seconds since epoch (RTC/NTP/cell time)
void port_random(void *buf, size_t len);    // must be a real entropy source on device
void port_log(const char *fmt, ...);

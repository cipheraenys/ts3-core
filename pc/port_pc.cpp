// PC implementation of port.h (Windows/MinGW or POSIX). For development and testing only.
#include "../src/port.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSE closesocket
#else
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSE close
#endif

int port_udp_open(const char *host, uint16_t port) {
#ifdef _WIN32
    static bool init = [] { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }();
    if (!init) return -1;
#endif
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", port);
    if (getaddrinfo(host, ps, &hints, &res) != 0) return -1;
    int s = (int)socket(res->ai_family, SOCK_DGRAM, 0);
    if (s >= 0 && connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) CLOSE(s), s = -1;
    freeaddrinfo(res);
    return s;
}

void port_udp_close(int s) { CLOSE(s); }

int port_udp_send(int s, const void *buf, size_t len) { return (int)send(s, (const char *)buf, (int)len, 0); }

int port_udp_recv(int s, void *buf, size_t len, uint32_t timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv{(long)(timeout_ms / 1000), (long)(timeout_ms % 1000) * 1000};
    int r = select(s + 1, &fds, nullptr, nullptr, &tv);
    if (r <= 0) return r;
    return (int)recv(s, (char *)buf, (int)len, 0);
}

uint32_t port_millis(void) {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint32_t port_unix_time(void) { return (uint32_t)time(nullptr); }

void port_random(void *buf, size_t len) {
    static std::random_device rd;
    for (size_t i = 0; i < len; i++) ((uint8_t *)buf)[i] = (uint8_t)rd();
}

void port_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

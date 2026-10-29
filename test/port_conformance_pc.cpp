// Host runner for port_conformance.cpp: runs it against an in-process UDP echo peer on 127.0.0.1.
//   port_conformance                 local peer (what ctest runs)
//   port_conformance HOST PORT       external UDP echo peer
// Copyright (c) 2026 cipheraenys@gmail.com, MIT license.
#include <cstdio>
#include <cstdlib>

#include "port_conformance.h"
#include "port.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSE closesocket
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSE close
#endif

static int g_peer = -1;

// Single-threaded on purpose: the test calls this between send and recv, so no thread library is needed.
static void echo_service() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(g_peer, &fds);
    timeval tv{1, 0};
    if (select(g_peer + 1, &fds, nullptr, nullptr, &tv) <= 0) return;
    char buf[2048];
    sockaddr_in from{};
    socklen_t fl = sizeof from;
    int n = (int)recvfrom(g_peer, buf, sizeof buf, 0, (sockaddr *)&from, &fl);
    if (n > 0) sendto(g_peer, buf, n, 0, (sockaddr *)&from, fl);
}

static bool peer_start(uint16_t *port) {
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
#endif
    g_peer = (int)socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t al = sizeof a;
    if (g_peer < 0 || bind(g_peer, (sockaddr *)&a, sizeof a) != 0 ||
        getsockname(g_peer, (sockaddr *)&a, &al) != 0)
        return false;
    *port = ntohs(a.sin_port);
    return true;
}

int main(int argc, char **argv) {
    if (argc == 3) return port_conformance_run(argv[1], (uint16_t)atoi(argv[2]), nullptr) != 0;
    uint16_t port;
    if (!peer_start(&port)) {
        fprintf(stderr, "cannot start local echo peer\n");
        return 2;
    }
    int fails = port_conformance_run("127.0.0.1", port, echo_service);
    CLOSE(g_peer);
    return fails != 0;
}

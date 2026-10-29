// Conformance checks for an implementation of core/port.h. See docs/PORT_CONTRACT.md.
// Copyright (c) 2026 cipheraenys@gmail.com, MIT license.
#pragma once
#include <cstdint>

// Runs every check and logs results through port_log. Returns the number of failed checks (0 = pass).
// echo_host:echo_port must be a UDP echo peer reachable through port_udp_open.
// service: called after each send to let an in-process peer answer; pass nullptr when the peer
// runs elsewhere (another task, another machine).
int port_conformance_run(const char *echo_host, uint16_t echo_port, void (*service)(void));

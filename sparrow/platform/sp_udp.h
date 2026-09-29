#ifndef SPARROW_PLATFORM_UDP_H
#define SPARROW_PLATFORM_UDP_H

#include <stddef.h>
#include <stdint.h>

#include "sp_status.h"

/* Non-blocking UDP endpoint with a fixed peer, used for the controller/HMI link. */
typedef struct {
    int fd;
    uint32_t peer_address; /* network byte order */
    uint16_t peer_port;    /* network byte order */
} SpUdp;

/* Binds listen_port on the loopback or given IPv4 address and sets the peer. */
SpStatus sp_udp_open(SpUdp *udp, const char *host, uint16_t listen_port, uint16_t peer_port);

/* Changes the peer to another IPv4 address and port. */
SpStatus sp_udp_set_peer(SpUdp *udp, const char *host, uint16_t port);

/* Best effort: returns SP_OK even if nobody is listening. */
SpStatus sp_udp_send(const SpUdp *udp, const void *data, size_t size);

/* Returns the datagram size, or 0 if none is pending. */
size_t sp_udp_receive(const SpUdp *udp, void *buffer, size_t capacity);

void sp_udp_close(SpUdp *udp);

#endif

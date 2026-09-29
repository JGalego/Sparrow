#include "sp_udp.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

SpStatus sp_udp_open(SpUdp *udp, const char *host, uint16_t listen_port, uint16_t peer_port)
{
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(listen_port)};
    const int reuse = 1;

    if (inet_pton(AF_INET, host, &local.sin_addr) != 1) {
        return SP_ERR_ARGUMENT;
    }
    udp->fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (udp->fd < 0) {
        return SP_ERR_IO;
    }
    setsockopt(udp->fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
    if (bind(udp->fd, (const struct sockaddr *)&local, sizeof local) != 0) {
        close(udp->fd);
        udp->fd = -1;
        return SP_ERR_IO;
    }
    udp->peer_address = local.sin_addr.s_addr;
    udp->peer_port = htons(peer_port);
    return SP_OK;
}

SpStatus sp_udp_send(const SpUdp *udp, const void *data, size_t size)
{
    const struct sockaddr_in peer = {
        .sin_family = AF_INET,
        .sin_port = udp->peer_port,
        .sin_addr = {.s_addr = udp->peer_address},
    };

    (void)sendto(udp->fd, data, size, 0, (const struct sockaddr *)&peer, sizeof peer);
    return SP_OK;
}

size_t sp_udp_receive(const SpUdp *udp, void *buffer, size_t capacity)
{
    const ssize_t received = recv(udp->fd, buffer, capacity, 0);

    return received > 0 ? (size_t)received : 0;
}

void sp_udp_close(SpUdp *udp)
{
    if (udp->fd >= 0) {
        close(udp->fd);
        udp->fd = -1;
    }
}

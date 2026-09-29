#ifndef BOILER_IO_UDP_H
#define BOILER_IO_UDP_H

#include <stdint.h>

#include "boiler_io.h"
#include "sp_udp.h"

/*
 * I/O backend for a plant simulated on another process or machine. The
 * plant sends INPUTS frames and receives OUTPUTS frames. Inputs older than
 * stale_after_ms are replaced by open-loop values.
 */
typedef struct {
    SpUdp udp;
    BoilerInputs latest;
    uint64_t latest_ns;
    uint64_t stale_after_ns;
    uint32_t sequence;
    uint32_t stale_reads;
} BoilerIoUdp;

SpStatus boiler_io_udp_open(BoilerIoUdp *io, const char *host, uint16_t listen_port,
                            uint16_t plant_port, uint32_t stale_after_ms);
BoilerIo boiler_io_udp_interface(BoilerIoUdp *io);

/* Forwards a raw frame (e.g. fault injection from the HMI) to the plant. */
void boiler_io_udp_forward(BoilerIoUdp *io, const void *frame, size_t size);

void boiler_io_udp_close(BoilerIoUdp *io);

#endif

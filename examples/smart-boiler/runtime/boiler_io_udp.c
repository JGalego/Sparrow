#include "boiler_io_udp.h"

#include <string.h>

#include "sp_frame.h"
#include "sp_period.h"

SpStatus boiler_io_udp_open(BoilerIoUdp *io, const char *host, uint16_t listen_port,
                            uint16_t plant_port, uint32_t stale_after_ms)
{
    memset(io, 0, sizeof *io);
    io->stale_after_ns = (uint64_t)stale_after_ms * 1000000u;
    io->latest = boiler_io_open_loop();
    return sp_udp_open(&io->udp, host, listen_port, plant_port);
}

static void drain(BoilerIoUdp *io)
{
    uint8_t buffer[SP_FRAME_MAX_SIZE];
    size_t size;

    while ((size = sp_udp_receive(&io->udp, buffer, sizeof buffer)) > 0) {
        SpFrameHeader header;
        const void *payload;

        if (sp_frame_decode(buffer, size, &header, &payload) == SP_OK &&
            header.kind == BOILER_FRAME_INPUTS && header.payload_size == sizeof(BoilerInputs)) {
            memcpy(&io->latest, payload, sizeof io->latest);
            io->latest_ns = sp_monotonic_ns();
        }
    }
}

static bool io_read(void *context, BoilerInputs *inputs)
{
    BoilerIoUdp *io = context;

    drain(io);
    if (io->latest_ns == 0 || sp_monotonic_ns() - io->latest_ns > io->stale_after_ns) {
        *inputs = boiler_io_open_loop();
        io->stale_reads++;
        return false;
    }
    *inputs = io->latest;
    return true;
}

static void io_write(void *context, const BoilerOutputs *outputs)
{
    BoilerIoUdp *io = context;
    uint8_t buffer[SP_FRAME_MAX_SIZE];
    const size_t size = sp_frame_encode(buffer, sizeof buffer, BOILER_FRAME_OUTPUTS, ++io->sequence,
                                        outputs, sizeof *outputs);

    if (size > 0) {
        sp_udp_send(&io->udp, buffer, size);
    }
}

BoilerIo boiler_io_udp_interface(BoilerIoUdp *io)
{
    const BoilerIo interface = {io_read, io_write, io};

    return interface;
}

void boiler_io_udp_forward(BoilerIoUdp *io, const void *frame, size_t size)
{
    sp_udp_send(&io->udp, frame, size);
}

void boiler_io_udp_close(BoilerIoUdp *io)
{
    sp_udp_close(&io->udp);
}

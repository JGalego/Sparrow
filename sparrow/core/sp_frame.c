#include "sp_frame.h"

#include <string.h>

size_t sp_frame_encode(uint8_t *buf, size_t capacity, uint16_t kind, uint32_t sequence,
                       const void *payload, size_t payload_size)
{
    SpFrameHeader header = {
        .magic = SP_FRAME_MAGIC,
        .version = SP_FRAME_VERSION,
        .kind = kind,
        .sequence = sequence,
        .payload_size = (uint32_t)payload_size,
    };

    if (payload_size > SP_FRAME_MAX_SIZE - SP_FRAME_HEADER_SIZE ||
        capacity < SP_FRAME_HEADER_SIZE + payload_size) {
        return 0;
    }
    memcpy(buf, &header, sizeof header);
    memcpy(buf + SP_FRAME_HEADER_SIZE, payload, payload_size);
    return SP_FRAME_HEADER_SIZE + payload_size;
}

SpStatus sp_frame_decode(const uint8_t *buf, size_t size, SpFrameHeader *header,
                         const void **payload)
{
    if (size < SP_FRAME_HEADER_SIZE) {
        return SP_ERR_SIZE;
    }
    memcpy(header, buf, sizeof *header);
    if (header->magic != SP_FRAME_MAGIC) {
        return SP_ERR_MAGIC;
    }
    if (header->version != SP_FRAME_VERSION) {
        return SP_ERR_VERSION;
    }
    if (header->payload_size != size - SP_FRAME_HEADER_SIZE) {
        return SP_ERR_SIZE;
    }
    *payload = buf + SP_FRAME_HEADER_SIZE;
    return SP_OK;
}

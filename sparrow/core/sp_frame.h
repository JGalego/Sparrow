#ifndef SPARROW_CORE_FRAME_H
#define SPARROW_CORE_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "sp_status.h"

/*
 * Datagram envelope shared by the controller, the simulator and the HMI.
 * Payloads are C structs copied verbatim, so both ends must use the same
 * struct layout. All supported targets are little-endian; the header refuses
 * to compile elsewhere rather than silently swap bytes.
 */
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "sp_frame requires a little-endian target"
#endif

#define SP_FRAME_MAGIC       UINT32_C(0x57505253) /* "SRPW" */
#define SP_FRAME_VERSION     1
#define SP_FRAME_HEADER_SIZE 16
#define SP_FRAME_MAX_SIZE    512

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t kind;
    uint32_t sequence;
    uint32_t payload_size;
} SpFrameHeader;

_Static_assert(sizeof(SpFrameHeader) == SP_FRAME_HEADER_SIZE, "frame header must be packed");

/* Writes header and payload to buf. Returns the frame size, or 0 if it does not fit. */
size_t sp_frame_encode(uint8_t *buf, size_t capacity, uint16_t kind, uint32_t sequence,
                       const void *payload, size_t payload_size);

/* Validates a received datagram. On SP_OK, *payload points into buf. */
SpStatus sp_frame_decode(const uint8_t *buf, size_t size, SpFrameHeader *header,
                         const void **payload);

#endif

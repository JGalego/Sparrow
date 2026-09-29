#include <string.h>

#include "sp_frame.h"
#include "sp_test.h"

SP_TEST(encoded_frame_round_trips, "")
{
    uint8_t buf[SP_FRAME_MAX_SIZE];
    const uint32_t payload = 0xCAFEBABE;
    SpFrameHeader header;
    const void *decoded;

    const size_t size = sp_frame_encode(buf, sizeof buf, 7, 42, &payload, sizeof payload);

    SP_ASSERT_EQ_INT(SP_FRAME_HEADER_SIZE + sizeof payload, size);
    SP_ASSERT_EQ_INT(SP_OK, sp_frame_decode(buf, size, &header, &decoded));
    SP_ASSERT_EQ_INT(7, header.kind);
    SP_ASSERT_EQ_INT(42, header.sequence);
    SP_ASSERT(memcmp(decoded, &payload, sizeof payload) == 0);
}

SP_TEST(encode_refuses_payload_larger_than_buffer, "")
{
    uint8_t buf[SP_FRAME_HEADER_SIZE + 3];
    const uint32_t payload = 1;

    SP_ASSERT_EQ_INT(0, sp_frame_encode(buf, sizeof buf, 1, 0, &payload, sizeof payload));
}

SP_TEST(decode_rejects_short_datagram, "")
{
    uint8_t buf[SP_FRAME_HEADER_SIZE - 1] = {0};
    SpFrameHeader header;
    const void *payload;

    SP_ASSERT_EQ_INT(SP_ERR_SIZE, sp_frame_decode(buf, sizeof buf, &header, &payload));
}

SP_TEST(decode_rejects_wrong_magic, "")
{
    uint8_t buf[SP_FRAME_MAX_SIZE];
    SpFrameHeader header;
    const void *payload;
    const size_t size = sp_frame_encode(buf, sizeof buf, 1, 0, "", 0);
    buf[0] ^= 0xFF;

    SP_ASSERT_EQ_INT(SP_ERR_MAGIC, sp_frame_decode(buf, size, &header, &payload));
}

SP_TEST(decode_rejects_unknown_version, "")
{
    uint8_t buf[SP_FRAME_MAX_SIZE];
    SpFrameHeader header;
    const void *payload;
    const size_t size = sp_frame_encode(buf, sizeof buf, 1, 0, "", 0);
    buf[4] = 99;

    SP_ASSERT_EQ_INT(SP_ERR_VERSION, sp_frame_decode(buf, size, &header, &payload));
}

SP_TEST(decode_rejects_length_mismatch, "")
{
    uint8_t buf[SP_FRAME_MAX_SIZE];
    const uint32_t payload_in = 5;
    SpFrameHeader header;
    const void *payload;
    const size_t size = sp_frame_encode(buf, sizeof buf, 1, 0, &payload_in, sizeof payload_in);

    SP_ASSERT_EQ_INT(SP_ERR_SIZE, sp_frame_decode(buf, size - 1, &header, &payload));
}

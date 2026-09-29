#include <math.h>

#include "sp_ring.h"
#include "sp_test.h"

static SpRing make_ring(float *storage, size_t capacity, int pushes)
{
    SpRing ring;
    sp_ring_init(&ring, storage, capacity);
    for (int i = 1; i <= pushes; i++) {
        sp_ring_push(&ring, (float)i);
    }
    return ring;
}

SP_TEST(empty_ring_has_no_samples, "REQ-031")
{
    float storage[4];
    SpRing ring = make_ring(storage, 4, 0);
    float value;

    SP_ASSERT_EQ_INT(0, sp_ring_count(&ring));
    SP_ASSERT(!sp_ring_get(&ring, 0, &value));
}

SP_TEST(newest_sample_has_age_zero, "REQ-031")
{
    float storage[4];
    SpRing ring = make_ring(storage, 4, 3);
    float value;

    SP_ASSERT(sp_ring_get(&ring, 0, &value));
    SP_ASSERT_NEAR(3.0, value, 0);
    SP_ASSERT(sp_ring_get(&ring, 2, &value));
    SP_ASSERT_NEAR(1.0, value, 0);
    SP_ASSERT(!sp_ring_get(&ring, 3, &value));
}

SP_TEST(oldest_sample_is_overwritten_when_full, "REQ-031")
{
    float storage[4];
    SpRing ring = make_ring(storage, 4, 6);
    float value;

    SP_ASSERT_EQ_INT(4, sp_ring_count(&ring));
    SP_ASSERT(sp_ring_get(&ring, 3, &value));
    SP_ASSERT_NEAR(3.0, value, 0);
    SP_ASSERT(sp_ring_get(&ring, 0, &value));
    SP_ASSERT_NEAR(6.0, value, 0);
}

SP_TEST(downsample_averages_buckets_oldest_first, "REQ-031")
{
    float storage[8];
    SpRing ring = make_ring(storage, 8, 8);
    float out[4];

    SP_ASSERT_EQ_INT(4, sp_ring_downsample(&ring, 8, out, 4));

    SP_ASSERT_NEAR(1.5, out[0], 1e-6);
    SP_ASSERT_NEAR(3.5, out[1], 1e-6);
    SP_ASSERT_NEAR(5.5, out[2], 1e-6);
    SP_ASSERT_NEAR(7.5, out[3], 1e-6);
}

SP_TEST(downsample_uses_only_the_requested_span, "REQ-031")
{
    float storage[8];
    SpRing ring = make_ring(storage, 8, 8);
    float out[2];

    sp_ring_downsample(&ring, 4, out, 2);

    SP_ASSERT_NEAR(5.5, out[0], 1e-6);
    SP_ASSERT_NEAR(7.5, out[1], 1e-6);
}

SP_TEST(downsample_marks_missing_history_as_nan, "REQ-031")
{
    float storage[8];
    SpRing ring = make_ring(storage, 8, 2);
    float out[4];

    sp_ring_downsample(&ring, 8, out, 4);

    SP_ASSERT(isnan(out[0]));
    SP_ASSERT(isnan(out[1]));
    SP_ASSERT(isnan(out[2]));
    SP_ASSERT_NEAR(1.5, out[3], 1e-6);
}

SP_TEST(downsample_rejects_invalid_arguments, "REQ-031")
{
    float storage[8];
    SpRing ring = make_ring(storage, 8, 8);
    float out[16];

    SP_ASSERT_EQ_INT(0, sp_ring_downsample(&ring, 8, out, 0));
    SP_ASSERT_EQ_INT(0, sp_ring_downsample(&ring, 4, out, 5));
    SP_ASSERT_EQ_INT(0, sp_ring_downsample(&ring, 9, out, 3));
}

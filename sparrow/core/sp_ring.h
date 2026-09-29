#ifndef SPARROW_CORE_RING_H
#define SPARROW_CORE_RING_H

#include <stdbool.h>
#include <stddef.h>

/* Fixed-capacity history of float samples. The caller owns the storage. */
typedef struct {
    float *data;
    size_t capacity;
    size_t head; /* index of the next write */
    size_t count;
} SpRing;

void sp_ring_init(SpRing *ring, float *storage, size_t capacity);
void sp_ring_push(SpRing *ring, float value);
size_t sp_ring_count(const SpRing *ring);

/* age 0 is the newest sample. Returns false if age is beyond the stored samples. */
bool sp_ring_get(const SpRing *ring, size_t age, float *value);

/*
 * Reduces the newest span samples to `points` values by averaging. out[0] is
 * the oldest bucket. Buckets that hold no stored sample are NAN. Requires
 * 0 < points <= span. Returns the number of values written (points) or 0 on
 * invalid arguments.
 */
size_t sp_ring_downsample(const SpRing *ring, size_t span, float *out, size_t points);

#endif

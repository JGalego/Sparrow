#include "sp_ring.h"

#include <math.h>

void sp_ring_init(SpRing *ring, float *storage, size_t capacity)
{
    ring->data = storage;
    ring->capacity = capacity;
    ring->head = 0;
    ring->count = 0;
}

void sp_ring_push(SpRing *ring, float value)
{
    ring->data[ring->head] = value;
    ring->head = (ring->head + 1) % ring->capacity;
    if (ring->count < ring->capacity) {
        ring->count++;
    }
}

size_t sp_ring_count(const SpRing *ring)
{
    return ring->count;
}

bool sp_ring_get(const SpRing *ring, size_t age, float *value)
{
    if (age >= ring->count) {
        return false;
    }
    *value = ring->data[(ring->head + ring->capacity - 1 - age) % ring->capacity];
    return true;
}

static float bucket_average(const SpRing *ring, size_t span, size_t first, size_t last)
{
    float sum = 0.0f;
    size_t used = 0;

    for (size_t index = first; index < last; index++) {
        float sample;
        if (sp_ring_get(ring, span - 1 - index, &sample)) {
            sum += sample;
            used++;
        }
    }
    return used > 0 ? sum / (float)used : NAN;
}

size_t sp_ring_downsample(const SpRing *ring, size_t span, float *out, size_t points)
{
    if (points == 0 || points > span || span > ring->capacity) {
        return 0;
    }
    for (size_t bucket = 0; bucket < points; bucket++) {
        const size_t first = bucket * span / points;
        const size_t last = (bucket + 1) * span / points;
        out[bucket] = bucket_average(ring, span, first, last);
    }
    return points;
}

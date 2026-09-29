#ifndef SPARROW_CORE_UTIL_H
#define SPARROW_CORE_UTIL_H

#include <math.h>
#include <stdbool.h>

static inline float sp_clampf(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/*
 * Rounds to 1/steps_per_unit. Limit checks run on quantized values so that a
 * reading of "110.0" compares exactly against a limit of 110.0 regardless of
 * the float error left by unit conversion.
 */
static inline float sp_quantize(float value, float steps_per_unit)
{
    return roundf(value * steps_per_unit) / steps_per_unit;
}

/*
 * Threshold with hysteresis for a "value too high" condition. Once active the
 * condition holds until the value falls to (limit - hysteresis) or below.
 */
static inline bool sp_high_with_hysteresis(bool active, float value, float limit, float hysteresis)
{
    if (active) {
        return value > limit - hysteresis;
    }
    return value > limit;
}

/* Same for a "value too low" condition. */
static inline bool sp_low_with_hysteresis(bool active, float value, float limit, float hysteresis)
{
    if (active) {
        return value < limit + hysteresis;
    }
    return value < limit;
}

#endif

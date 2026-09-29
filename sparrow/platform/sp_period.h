#ifndef SPARROW_PLATFORM_PERIOD_H
#define SPARROW_PLATFORM_PERIOD_H

#include <stdint.h>

/*
 * Fixed-rate loop timing on CLOCK_MONOTONIC with absolute deadlines, so
 * scheduling jitter does not accumulate. A cycle that finishes after its
 * next deadline counts as an overrun, and the schedule restarts from now
 * instead of trying to catch up.
 */
typedef struct {
    uint64_t period_ns;
    uint64_t next_ns;
    uint64_t last_ns;
    uint32_t overruns;
} SpPeriod;

void sp_period_start(SpPeriod *period, uint32_t period_us);

/* Sleeps until the next deadline. Returns the milliseconds since the previous wake-up. */
uint32_t sp_period_wait(SpPeriod *period);

uint64_t sp_monotonic_ns(void);

#endif

#include "sp_period.h"

#include <errno.h>
#include <time.h>

#define NS_PER_S  1000000000ull
#define NS_PER_MS 1000000ull

uint64_t sp_monotonic_ns(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * NS_PER_S + (uint64_t)now.tv_nsec;
}

void sp_period_start(SpPeriod *period, uint32_t period_us)
{
    period->period_ns = (uint64_t)period_us * 1000u;
    period->last_ns = sp_monotonic_ns();
    period->next_ns = period->last_ns + period->period_ns;
    period->overruns = 0;
}

static void sleep_until(uint64_t deadline_ns)
{
    const struct timespec deadline = {
        .tv_sec = (time_t)(deadline_ns / NS_PER_S),
        .tv_nsec = (long)(deadline_ns % NS_PER_S),
    };

    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL) == EINTR) {
    }
}

uint32_t sp_period_wait(SpPeriod *period)
{
    uint64_t now = sp_monotonic_ns();
    uint64_t elapsed_ns;

    if (now > period->next_ns) {
        period->overruns++;
        period->next_ns = now;
    } else {
        sleep_until(period->next_ns);
        now = sp_monotonic_ns();
    }
    elapsed_ns = now - period->last_ns;
    period->last_ns = now;
    period->next_ns += period->period_ns;
    return (uint32_t)((elapsed_ns + NS_PER_MS / 2) / NS_PER_MS);
}

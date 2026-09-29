#ifndef SPARROW_CORE_PERSIST_H
#define SPARROW_CORE_PERSIST_H

#include <stdbool.h>
#include <stdint.h>

/* Confirms that a condition has been true for a minimum continuous time. */
typedef struct {
    uint32_t elapsed_ms;
} SpPersist;

void sp_persist_reset(SpPersist *persist);

/*
 * Advances the timer by dt_ms while condition is true, clears it otherwise.
 * Returns true once the condition has held for required_ms. A required time of
 * zero confirms on the first true sample.
 */
bool sp_persist_update(SpPersist *persist, bool condition, uint32_t required_ms, uint32_t dt_ms);

#endif

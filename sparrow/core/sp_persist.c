#include "sp_persist.h"

void sp_persist_reset(SpPersist *persist)
{
    persist->elapsed_ms = 0;
}

bool sp_persist_update(SpPersist *persist, bool condition, uint32_t required_ms, uint32_t dt_ms)
{
    if (!condition) {
        persist->elapsed_ms = 0;
        return false;
    }
    if (persist->elapsed_ms < UINT32_MAX - dt_ms) {
        persist->elapsed_ms += dt_ms;
    } else {
        persist->elapsed_ms = UINT32_MAX;
    }
    return persist->elapsed_ms >= required_ms;
}

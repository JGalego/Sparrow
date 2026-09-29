#include <stdint.h>

#include "sp_persist.h"
#include "sp_test.h"

SP_TEST(confirms_only_after_required_time, "")
{
    SpPersist timer = {0};

    SP_ASSERT(!sp_persist_update(&timer, true, 500, 100));
    SP_ASSERT(!sp_persist_update(&timer, true, 500, 100));
    SP_ASSERT(!sp_persist_update(&timer, true, 500, 100));
    SP_ASSERT(!sp_persist_update(&timer, true, 500, 100));
    SP_ASSERT(sp_persist_update(&timer, true, 500, 100));
}

SP_TEST(interruption_restarts_the_timer, "")
{
    SpPersist timer = {0};
    sp_persist_update(&timer, true, 300, 200);

    sp_persist_update(&timer, false, 300, 100);

    SP_ASSERT(!sp_persist_update(&timer, true, 300, 200));
    SP_ASSERT(sp_persist_update(&timer, true, 300, 100));
}

SP_TEST(zero_required_time_confirms_immediately, "")
{
    SpPersist timer = {0};

    SP_ASSERT(sp_persist_update(&timer, true, 0, 100));
    SP_ASSERT(!sp_persist_update(&timer, false, 0, 100));
}

SP_TEST(elapsed_time_saturates, "")
{
    SpPersist timer = {UINT32_MAX - 10};

    sp_persist_update(&timer, true, 1000, 100);

    SP_ASSERT_EQ_INT(UINT32_MAX, timer.elapsed_ms);
}

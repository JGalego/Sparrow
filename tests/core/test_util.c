#include "sp_test.h"
#include "sp_util.h"

SP_TEST(quantize_makes_boundary_values_exact, "")
{
    SP_ASSERT(sp_quantize(110.00001f, 10.0f) == 110.0f);
    SP_ASSERT(sp_quantize(109.94f, 10.0f) == 109.9f);
    SP_ASSERT(sp_quantize(110.06f, 10.0f) == 110.1f);
}

SP_TEST(high_condition_needs_the_limit_to_be_exceeded, "")
{
    SP_ASSERT(!sp_high_with_hysteresis(false, 110.0f, 110.0f, 3.0f));
    SP_ASSERT(sp_high_with_hysteresis(false, 110.1f, 110.0f, 3.0f));
}

SP_TEST(high_condition_holds_inside_the_hysteresis_band, "")
{
    SP_ASSERT(sp_high_with_hysteresis(true, 108.0f, 110.0f, 3.0f));
    SP_ASSERT(sp_high_with_hysteresis(true, 107.1f, 110.0f, 3.0f));
    SP_ASSERT(!sp_high_with_hysteresis(true, 107.0f, 110.0f, 3.0f));
}

SP_TEST(low_condition_uses_hysteresis_upwards, "")
{
    SP_ASSERT(!sp_low_with_hysteresis(false, 0.8f, 0.8f, 0.2f));
    SP_ASSERT(sp_low_with_hysteresis(false, 0.79f, 0.8f, 0.2f));
    SP_ASSERT(sp_low_with_hysteresis(true, 0.95f, 0.8f, 0.2f));
    SP_ASSERT(!sp_low_with_hysteresis(true, 1.0f, 0.8f, 0.2f));
}

SP_TEST(clamp_limits_both_ends, "")
{
    SP_ASSERT_NEAR(0.0, sp_clampf(-1.0f, 0.0f, 10.0f), 0);
    SP_ASSERT_NEAR(10.0, sp_clampf(11.0f, 0.0f, 10.0f), 0);
    SP_ASSERT_NEAR(5.0, sp_clampf(5.0f, 0.0f, 10.0f), 0);
}

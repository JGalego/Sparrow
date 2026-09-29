#include "sp_pi.h"
#include "sp_test.h"

SP_TEST(proportional_term_scales_with_error, "")
{
    SpPi pi;
    sp_pi_init(&pi, 2.0f, 0.0f, 0.0f, 100.0f);

    SP_ASSERT_NEAR(20.0, sp_pi_step(&pi, 10.0f, 0.1f), 1e-4);
}

SP_TEST(output_is_clamped, "")
{
    SpPi pi;
    sp_pi_init(&pi, 10.0f, 0.0f, 0.0f, 100.0f);

    SP_ASSERT_NEAR(100.0, sp_pi_step(&pi, 50.0f, 0.1f), 1e-4);
    SP_ASSERT_NEAR(0.0, sp_pi_step(&pi, -50.0f, 0.1f), 1e-4);
}

SP_TEST(integral_accumulates_over_time, "")
{
    SpPi pi;
    sp_pi_init(&pi, 0.0f, 1.0f, 0.0f, 100.0f);

    sp_pi_step(&pi, 5.0f, 1.0f);
    SP_ASSERT_NEAR(10.0, sp_pi_step(&pi, 5.0f, 1.0f), 1e-4);
}

SP_TEST(integral_does_not_wind_up_while_saturated, "")
{
    SpPi pi;
    sp_pi_init(&pi, 10.0f, 1.0f, 0.0f, 100.0f);

    for (int i = 0; i < 1000; i++) {
        sp_pi_step(&pi, 50.0f, 1.0f);
    }

    SP_ASSERT_NEAR(0.0, pi.integral, 1e-4);
}

SP_TEST(integral_recovers_immediately_after_saturation, "")
{
    SpPi pi;
    sp_pi_init(&pi, 10.0f, 1.0f, 0.0f, 100.0f);
    for (int i = 0; i < 1000; i++) {
        sp_pi_step(&pi, 50.0f, 1.0f);
    }

    SP_ASSERT_NEAR(0.0, sp_pi_step(&pi, -1.0f, 1.0f), 1e-4);
}

SP_TEST(reset_clears_integral, "")
{
    SpPi pi;
    sp_pi_init(&pi, 0.0f, 1.0f, 0.0f, 100.0f);
    sp_pi_step(&pi, 5.0f, 1.0f);

    sp_pi_reset(&pi);

    SP_ASSERT_NEAR(0.0, pi.integral, 1e-9);
}

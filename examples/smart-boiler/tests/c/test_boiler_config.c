#include <math.h>
#include <string.h>

#include "boiler_controller.h"
#include "sp_test.h"

static SpStatus init_with(const BoilerConfig *config, const char **reason)
{
    BoilerController controller;

    return boiler_init(&controller, config, reason);
}

SP_TEST(default_configuration_is_accepted, "REQ-021")
{
    const BoilerConfig config = boiler_config_default();

    SP_ASSERT_EQ_INT(SP_OK, init_with(&config, NULL));
}

SP_TEST(parameter_outside_its_range_is_rejected_and_named, "REQ-021")
{
    BoilerConfig config = boiler_config_default();
    const char *reason = NULL;
    config.flow_min_lpm = 0.0f;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, init_with(&config, &reason));
    SP_ASSERT(reason != NULL && strcmp(reason, "flow_min_lpm") == 0);
}

SP_TEST(nan_parameter_is_rejected, "REQ-021")
{
    BoilerConfig config = boiler_config_default();
    config.heater_kp = NAN;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, init_with(&config, NULL));
}

SP_TEST(integer_parameter_outside_its_range_is_rejected, "REQ-021")
{
    BoilerConfig config = boiler_config_default();
    config.valve_travel_timeout_ms = 500;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, init_with(&config, NULL));
}

SP_TEST(warning_limit_must_be_below_the_trip_limit, "REQ-021")
{
    BoilerConfig config = boiler_config_default();
    config.temperature_warn_c = config.temperature_trip_c;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, init_with(&config, NULL));
}

SP_TEST(trip_limit_must_lie_inside_the_sensor_span, "REQ-021")
{
    BoilerConfig config = boiler_config_default();
    config.temperature_max_c = 105.0f;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, init_with(&config, NULL));
}

SP_TEST(pressure_limits_must_be_ordered, "REQ-021")
{
    BoilerConfig config = boiler_config_default();
    config.pressure_low_bar = 3.6f;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, init_with(&config, NULL));
}

SP_TEST(setpoint_range_must_stay_below_the_warning_limit, "REQ-021")
{
    BoilerConfig config = boiler_config_default();
    config.setpoint_max_c = 100.0f;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, init_with(&config, NULL));
}

SP_TEST(default_setpoint_must_lie_within_the_setpoint_range, "REQ-021")
{
    BoilerConfig config = boiler_config_default();
    config.setpoint_default_c = 30.0f;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, init_with(&config, NULL));
}

SP_TEST(valve_thresholds_must_be_ordered, "REQ-021")
{
    BoilerConfig config = boiler_config_default();
    config.valve_closed_pct = 60.0f;
    config.valve_open_pct = 55.0f;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, init_with(&config, NULL));
}

SP_TEST(rejected_configuration_leaves_the_controller_untouched, "REQ-021")
{
    BoilerController controller;
    BoilerController reference;
    BoilerConfig config = boiler_config_default();
    memset(&controller, 0xAB, sizeof controller);
    memcpy(&reference, &controller, sizeof controller);
    config.heater_ki = -1.0f;

    SP_ASSERT_EQ_INT(SP_ERR_RANGE, boiler_init(&controller, &config, NULL));
    SP_ASSERT(memcmp(&controller, &reference, sizeof controller) == 0);
}

#include <math.h>

#include "boiler_fixture.h"
#include "boiler_measure.h"
#include "sp_test.h"

static BoilerMeasurements measure_default(const BoilerInputs *inputs)
{
    const BoilerConfig config = boiler_config_default();

    return boiler_measure(&config, inputs);
}

SP_TEST(loop_end_points_map_to_the_configured_span, "REQ-001")
{
    BoilerInputs inputs = {
        .temperature_ma = 4.0f, .pressure_ma = 20.0f, .flow_ma = 12.0f, .valve_position_ma = 20.0f};

    const BoilerMeasurements m = measure_default(&inputs);

    SP_ASSERT(m.temperature_c.valid);
    SP_ASSERT_NEAR(0.0, m.temperature_c.value, 1e-4);
    SP_ASSERT_NEAR(6.0, m.pressure_bar.value, 1e-4);
    SP_ASSERT_NEAR(50.0, m.flow_lpm.value, 1e-4);
    SP_ASSERT_NEAR(100.0, m.valve_position_pct.value, 1e-4);
}

SP_TEST(mid_scale_temperature_is_75_degrees, "REQ-001")
{
    BoilerInputs inputs = {
        .temperature_ma = 12.0f, .pressure_ma = 4.0f, .flow_ma = 4.0f, .valve_position_ma = 4.0f};

    SP_ASSERT_NEAR(75.0, measure_default(&inputs).temperature_c.value, 1e-4);
}

SP_TEST(readings_are_rounded_to_transmitter_resolution, "REQ-001")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_set_temperature(&f, 110.04f);

    const BoilerMeasurements m = measure_default(&f.inputs);

    SP_ASSERT(m.temperature_c.value == 110.0f);
}

SP_TEST(current_at_the_fault_thresholds_is_still_valid, "REQ-002")
{
    BoilerInputs inputs = {
        .temperature_ma = 3.6f, .pressure_ma = 21.0f, .flow_ma = 4.0f, .valve_position_ma = 4.0f};

    const BoilerMeasurements m = measure_default(&inputs);

    SP_ASSERT(m.temperature_c.valid);
    SP_ASSERT(m.pressure_bar.valid);
}

SP_TEST(current_beyond_the_fault_thresholds_is_invalid, "REQ-002")
{
    BoilerInputs inputs = {.temperature_ma = 3.59f,
                           .pressure_ma = 21.01f,
                           .flow_ma = 0.0f,
                           .valve_position_ma = 25.0f};

    const BoilerMeasurements m = measure_default(&inputs);

    SP_ASSERT(!m.temperature_c.valid);
    SP_ASSERT(!m.pressure_bar.valid);
    SP_ASSERT(!m.flow_lpm.valid);
    SP_ASSERT(!m.valve_position_pct.valid);
}

SP_TEST(nan_current_is_invalid, "REQ-002")
{
    BoilerInputs inputs = {
        .temperature_ma = NAN, .pressure_ma = 4.0f, .flow_ma = 4.0f, .valve_position_ma = 4.0f};

    SP_ASSERT(!measure_default(&inputs).temperature_c.valid);
}

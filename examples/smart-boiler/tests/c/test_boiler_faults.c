#include "boiler_fixture.h"
#include "sp_test.h"

/* The fault is active now or latched from an earlier activation. */
static bool fault_raised(const BoilerFixture *f, BoilerFault fault)
{
    return ((f->status.alarms_active | f->status.alarms_latched) & BOILER_FAULT_BIT(fault)) != 0;
}

/* The fault condition is present in this step, ignoring latching. */
static bool condition_present(const BoilerFixture *f, BoilerFault fault)
{
    return (f->status.alarms_active & BOILER_FAULT_BIT(fault)) != 0;
}

static BoilerFixture standby_fixture(void)
{
    BoilerFixture f;

    fixture_init(&f);
    fixture_reach_standby(&f);
    return f;
}

/* Over-temperature: limit 110.0 degC, trips when exceeded. */

SP_TEST(over_temperature_does_not_trip_below_the_limit, "REQ-006")
{
    BoilerFixture f = standby_fixture();

    fixture_set_temperature(&f, 109.9f);
    fixture_step(&f);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_OVER_TEMP));
    SP_ASSERT(f.status.state != BOILER_STATE_FAULT);
}

SP_TEST(over_temperature_does_not_trip_at_the_limit, "REQ-006")
{
    BoilerFixture f = standby_fixture();

    fixture_set_temperature(&f, 110.0f);
    fixture_step(&f);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_OVER_TEMP));
}

SP_TEST(over_temperature_trips_above_the_limit, "REQ-006")
{
    BoilerFixture f = standby_fixture();

    fixture_set_temperature(&f, 110.1f);
    fixture_step(&f);

    SP_ASSERT(fault_raised(&f, BOILER_FAULT_OVER_TEMP));
    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
    SP_ASSERT_NEAR(0.0, f.outputs.heater_power_pct, 0);
}

SP_TEST(over_temperature_trip_opens_the_contactor_while_running, "REQ-006,REQ-016")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_running(&f);
    fixture_set_temperature(&f, 80.0f);
    fixture_step_following(&f, 1000);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT_EQ_INT(1, f.outputs.heater_contactor);

    fixture_set_temperature(&f, 110.1f);
    fixture_step(&f);

    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
    SP_ASSERT_NEAR(0.0, f.outputs.heater_power_pct, 0);
}

SP_TEST(over_temperature_stays_latched_after_the_temperature_drops, "REQ-006,REQ-015")
{
    BoilerFixture f = standby_fixture();
    fixture_set_temperature(&f, 111.0f);
    fixture_step(&f);

    fixture_set_temperature(&f, 60.0f);
    fixture_run_ms(&f, 2000);

    SP_ASSERT(!condition_present(&f, BOILER_FAULT_OVER_TEMP));
    SP_ASSERT((f.status.alarms_latched & BOILER_FAULT_BIT(BOILER_FAULT_OVER_TEMP)) != 0);
    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
}

SP_TEST(over_temperature_condition_holds_inside_the_hysteresis_band, "REQ-006")
{
    BoilerFixture f = standby_fixture();
    fixture_set_temperature(&f, 111.0f);
    fixture_step(&f);

    fixture_set_temperature(&f, 107.1f);
    fixture_step(&f);
    SP_ASSERT(condition_present(&f, BOILER_FAULT_OVER_TEMP));

    fixture_set_temperature(&f, 107.0f);
    fixture_step(&f);
    SP_ASSERT(!condition_present(&f, BOILER_FAULT_OVER_TEMP));
}

/* High-temperature warning: limit 100.0 degC, persists for 1 s. */

SP_TEST(high_temperature_warning_needs_the_limit_to_be_exceeded, "REQ-007")
{
    BoilerFixture f = standby_fixture();

    fixture_set_temperature(&f, 100.0f);
    fixture_run_ms(&f, 3000);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_TEMP_HIGH));
}

SP_TEST(high_temperature_warning_appears_after_one_second, "REQ-007")
{
    BoilerFixture f = standby_fixture();
    fixture_set_temperature(&f, 100.1f);

    fixture_run_ms(&f, 900);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_TEMP_HIGH));

    fixture_run_ms(&f, 200);
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_TEMP_HIGH));
}

SP_TEST(high_temperature_warning_does_not_force_the_fault_state, "REQ-007")
{
    BoilerFixture f = standby_fixture();

    fixture_set_temperature(&f, 105.0f);
    fixture_run_ms(&f, 3000);

    SP_ASSERT(fault_raised(&f, BOILER_FAULT_TEMP_HIGH));
    SP_ASSERT_EQ_INT(BOILER_STATE_STANDBY, f.status.state);
}

SP_TEST(high_temperature_warning_clears_without_latching, "REQ-007")
{
    BoilerFixture f = standby_fixture();
    fixture_set_temperature(&f, 105.0f);
    fixture_run_ms(&f, 2000);

    fixture_set_temperature(&f, 96.9f);
    fixture_step(&f);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_TEMP_HIGH));
    SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
}

/* Pressure: warning 3.50 bar, trip 4.00 bar, low 0.80 bar for 2 s. */

SP_TEST(over_pressure_boundaries, "REQ-008")
{
    BoilerFixture f = standby_fixture();

    fixture_set_pressure(&f, 3.99f);
    fixture_step(&f);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_OVER_PRESSURE));

    fixture_set_pressure(&f, 4.0f);
    fixture_step(&f);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_OVER_PRESSURE));

    fixture_set_pressure(&f, 4.01f);
    fixture_step(&f);
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_OVER_PRESSURE));
    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
}

SP_TEST(high_pressure_warning_boundaries, "REQ-009")
{
    BoilerFixture f = standby_fixture();

    fixture_set_pressure(&f, 3.5f);
    fixture_run_ms(&f, 2000);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_PRESSURE_HIGH));

    fixture_set_pressure(&f, 3.51f);
    fixture_run_ms(&f, 900);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_PRESSURE_HIGH));
    fixture_run_ms(&f, 200);
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_PRESSURE_HIGH));
    SP_ASSERT_EQ_INT(BOILER_STATE_STANDBY, f.status.state);
}

SP_TEST(low_pressure_at_the_limit_is_accepted, "REQ-010")
{
    BoilerFixture f = standby_fixture();

    fixture_set_pressure(&f, 0.8f);
    fixture_run_ms(&f, 5000);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_LOW_PRESSURE));
}

SP_TEST(low_pressure_trips_after_two_seconds, "REQ-010")
{
    BoilerFixture f = standby_fixture();
    fixture_set_pressure(&f, 0.79f);

    fixture_run_ms(&f, 1900);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_LOW_PRESSURE));

    fixture_run_ms(&f, 200);
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_LOW_PRESSURE));
    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
}

SP_TEST(short_pressure_dip_does_not_trip, "REQ-010")
{
    BoilerFixture f = standby_fixture();

    fixture_set_pressure(&f, 0.5f);
    fixture_run_ms(&f, 1500);
    fixture_set_pressure(&f, 1.2f);
    fixture_run_ms(&f, 3000);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_LOW_PRESSURE));
    SP_ASSERT_EQ_INT(BOILER_STATE_STANDBY, f.status.state);
}

/* Sensors: loop current outside 3.6..21.0 mA for 500 ms. */

SP_TEST(sensor_fault_is_raised_after_the_debounce_time, "REQ-002")
{
    BoilerFixture f = standby_fixture();
    f.inputs.temperature_ma = 3.5f;

    fixture_run_ms(&f, 400);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_TEMP_SENSOR));
    SP_ASSERT_EQ_INT(0, f.status.temperature_valid);

    fixture_run_ms(&f, 100);
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_TEMP_SENSOR));
    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
}

SP_TEST(short_circuit_current_raises_a_sensor_fault, "REQ-002")
{
    BoilerFixture f = standby_fixture();
    f.inputs.pressure_ma = 21.5f;

    fixture_run_ms(&f, 600);

    SP_ASSERT(fault_raised(&f, BOILER_FAULT_PRESSURE_SENSOR));
}

SP_TEST(each_sensor_has_its_own_fault, "REQ-002")
{
    BoilerFixture f = standby_fixture();
    f.inputs.flow_ma = 0.0f;
    f.inputs.valve_position_ma = 0.0f;

    fixture_run_ms(&f, 600);

    SP_ASSERT(fault_raised(&f, BOILER_FAULT_FLOW_SENSOR));
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_VALVE_SENSOR));
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_TEMP_SENSOR));
}

SP_TEST(brief_sensor_dropout_is_ignored, "REQ-002")
{
    BoilerFixture f = standby_fixture();

    f.inputs.temperature_ma = 0.0f;
    fixture_run_ms(&f, 300);
    fixture_set_temperature(&f, 20.0f);
    fixture_run_ms(&f, 1000);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_TEMP_SENSOR));
    SP_ASSERT_EQ_INT(BOILER_STATE_STANDBY, f.status.state);
}

/* Pump: contactor feedback must follow the command within 2 s. */

SP_TEST(pump_feedback_mismatch_trips_after_the_delay, "REQ-011")
{
    BoilerFixture f = standby_fixture();
    const BoilerCommands pump_on = {.pump_manual = 1};
    fixture_command(&f, pump_on);

    fixture_run_ms(&f, 1800);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_PUMP_FAILURE));

    fixture_run_ms(&f, 400);
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_PUMP_FAILURE));
}

SP_TEST(pump_running_without_command_is_a_pump_failure, "REQ-011")
{
    BoilerFixture f = standby_fixture();
    f.inputs.pump_running = 1;

    fixture_run_ms(&f, 2200);

    SP_ASSERT(fault_raised(&f, BOILER_FAULT_PUMP_FAILURE));
}

SP_TEST(pump_feedback_that_follows_the_command_is_healthy, "REQ-011")
{
    BoilerFixture f = standby_fixture();
    const BoilerCommands pump_on = {.pump_manual = 1};
    fixture_command(&f, pump_on);
    f.inputs.pump_running = 1;

    fixture_run_ms(&f, 5000);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_PUMP_FAILURE));
}

/* Flow: required 5 s after pump start, loss confirmed after 3 s. */

SP_TEST(missing_flow_trips_eight_seconds_after_pump_start, "REQ-012")
{
    BoilerFixture f = standby_fixture();
    const BoilerCommands pump_on = {.pump_manual = 1};
    fixture_command(&f, pump_on);
    f.inputs.pump_running = 1;

    fixture_run_ms(&f, 7500);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_NO_FLOW));

    fixture_run_ms(&f, 1000);
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_NO_FLOW));
}

SP_TEST(flow_at_the_minimum_is_not_a_fault, "REQ-012")
{
    BoilerFixture f = standby_fixture();
    const BoilerCommands pump_on = {.pump_manual = 1};
    fixture_command(&f, pump_on);
    f.inputs.pump_running = 1;
    fixture_set_flow(&f, 5.0f);

    fixture_run_ms(&f, 15000);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_NO_FLOW));
}

SP_TEST(flow_below_the_minimum_is_a_fault, "REQ-012")
{
    BoilerFixture f = standby_fixture();
    const BoilerCommands pump_on = {.pump_manual = 1};
    fixture_command(&f, pump_on);
    f.inputs.pump_running = 1;
    fixture_set_flow(&f, 4.9f);

    fixture_run_ms(&f, 9000);

    SP_ASSERT(fault_raised(&f, BOILER_FAULT_NO_FLOW));
}

/* Valve: end position must be reached within 12 s. */

SP_TEST(valve_that_does_not_open_trips_after_the_travel_timeout, "REQ-014")
{
    BoilerFixture f = standby_fixture();
    const BoilerCommands valve_open = {.valve_manual = 1};
    fixture_command(&f, valve_open);

    fixture_run_ms(&f, 11500);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_VALVE_FAILURE));

    fixture_run_ms(&f, 1000);
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_VALVE_FAILURE));
}

SP_TEST(valve_that_does_not_close_trips_after_the_travel_timeout, "REQ-014")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_set_valve_position(&f, 100.0f);

    fixture_run_ms(&f, 13000);

    SP_ASSERT(fault_raised(&f, BOILER_FAULT_VALVE_FAILURE));
}

SP_TEST(valve_position_boundaries, "REQ-014")
{
    BoilerFixture f = standby_fixture();
    const BoilerCommands valve_open = {.valve_manual = 1};
    fixture_command(&f, valve_open);

    fixture_set_valve_position(&f, 90.0f);
    fixture_run_ms(&f, 15000);
    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_VALVE_FAILURE));

    fixture_set_valve_position(&f, 89.9f);
    fixture_step(&f);
    SP_ASSERT(fault_raised(&f, BOILER_FAULT_VALVE_FAILURE));
}

SP_TEST(valve_that_reaches_its_position_in_time_is_healthy, "REQ-014")
{
    BoilerFixture f = standby_fixture();
    const BoilerCommands valve_open = {.valve_manual = 1};
    fixture_command(&f, valve_open);

    fixture_run_ms(&f, 8000);
    fixture_set_valve_position(&f, 100.0f);
    fixture_run_ms(&f, 10000);

    SP_ASSERT(!fault_raised(&f, BOILER_FAULT_VALVE_FAILURE));
}

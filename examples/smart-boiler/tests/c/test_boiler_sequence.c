#include <math.h>

#include "boiler_fixture.h"
#include "sp_test.h"

static BoilerFixture running_fixture(void)
{
    BoilerFixture f;

    fixture_init(&f);
    fixture_reach_running(&f);
    return f;
}

SP_TEST(controller_starts_in_init_with_everything_off, "REQ-003")
{
    BoilerFixture f;
    fixture_init(&f);

    fixture_run_ms(&f, 500);

    SP_ASSERT_EQ_INT(BOILER_STATE_INIT, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
    SP_ASSERT_EQ_INT(0, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(0, f.outputs.valve_open);
}

SP_TEST(init_ends_after_the_configured_time, "REQ-003")
{
    BoilerFixture f;
    fixture_init(&f);

    fixture_run_ms(&f, 900);
    SP_ASSERT_EQ_INT(BOILER_STATE_INIT, f.status.state);

    fixture_run_ms(&f, 200);
    SP_ASSERT_EQ_INT(BOILER_STATE_STANDBY, f.status.state);
}

SP_TEST(start_is_ignored_during_init, "REQ-003")
{
    BoilerFixture f;
    fixture_init(&f);
    const BoilerCommands start = {.start = 1};

    fixture_command(&f, start);

    SP_ASSERT_EQ_INT(BOILER_STATE_INIT, f.status.state);
}

SP_TEST(startup_opens_the_valve_before_starting_the_pump, "REQ-003")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);
    const BoilerCommands start = {.start = 1};

    fixture_command(&f, start);
    fixture_run_ms(&f, 3000);

    SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
    SP_ASSERT_EQ_INT(1, f.outputs.valve_open);
    SP_ASSERT_EQ_INT(0, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
}

SP_TEST(pump_starts_once_the_valve_position_reaches_the_open_limit, "REQ-003")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);
    const BoilerCommands start = {.start = 1};
    fixture_command(&f, start);

    fixture_set_valve_position(&f, 89.9f);
    fixture_step(&f);
    SP_ASSERT_EQ_INT(0, f.outputs.pump_run);

    fixture_set_valve_position(&f, 90.0f);
    fixture_step(&f);
    SP_ASSERT_EQ_INT(1, f.outputs.pump_run);
}

SP_TEST(startup_completes_when_flow_is_proven, "REQ-003")
{
    BoilerFixture f = running_fixture();

    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT_EQ_INT(0, (int)f.status.alarms_active);
}

SP_TEST(startup_waits_for_flow, "REQ-003")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);
    const BoilerCommands start = {.start = 1};
    fixture_command(&f, start);

    for (int i = 0; i < 40; i++) {
        fixture_step(&f);
        f.inputs.pump_running = f.outputs.pump_run;
        fixture_set_valve_position(&f, 100.0f);
    }

    SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
}

SP_TEST(stop_during_startup_goes_to_shutdown, "REQ-003,REQ-017")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);
    const BoilerCommands start = {.start = 1};
    const BoilerCommands stop = {.stop = 1};
    fixture_command(&f, start);

    fixture_command(&f, stop);

    SP_ASSERT_EQ_INT(BOILER_STATE_SHUTDOWN, f.status.state);
}

SP_TEST(start_is_refused_while_a_critical_fault_is_standing, "REQ-015")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);
    fixture_set_pressure(&f, 4.5f);
    fixture_step(&f);
    const BoilerCommands start = {.start = 1};

    fixture_command(&f, start);

    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
}

/* Heater regulation and interlock */

SP_TEST(heater_runs_at_full_power_far_below_the_setpoint, "REQ-004")
{
    BoilerFixture f = running_fixture();

    fixture_set_temperature(&f, 40.0f);
    fixture_step_following(&f, 500);

    SP_ASSERT_NEAR(100.0, f.outputs.heater_power_pct, 1e-3);
    SP_ASSERT_EQ_INT(1, f.outputs.heater_contactor);
}

SP_TEST(heater_is_off_above_the_setpoint, "REQ-004")
{
    BoilerFixture f = running_fixture();

    fixture_set_temperature(&f, 85.0f);
    fixture_step_following(&f, 500);

    SP_ASSERT_NEAR(0.0, f.outputs.heater_power_pct, 1e-3);
}

SP_TEST(heater_power_falls_as_the_setpoint_is_approached, "REQ-004")
{
    BoilerFixture f = running_fixture();

    fixture_set_temperature(&f, 79.0f);
    fixture_step_following(&f, 100);

    SP_ASSERT(f.outputs.heater_power_pct > 0.0f);
    SP_ASSERT(f.outputs.heater_power_pct < 20.0f);
}

SP_TEST(heater_is_disabled_when_flow_falls_below_the_minimum, "REQ-013")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 60.0f);
    fixture_step_following(&f, 300);
    SP_ASSERT(f.outputs.heater_power_pct > 0.0f);

    fixture_set_flow(&f, 4.9f);
    fixture_step(&f);

    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
    SP_ASSERT_NEAR(0.0, f.outputs.heater_power_pct, 0);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
}

SP_TEST(heater_is_enabled_with_flow_at_the_minimum, "REQ-013")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 60.0f);

    fixture_set_flow(&f, 5.0f);
    fixture_step(&f);

    SP_ASSERT_EQ_INT(1, f.outputs.heater_contactor);
}

SP_TEST(heater_is_disabled_when_the_pump_feedback_is_lost, "REQ-013")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 60.0f);

    f.inputs.pump_running = 0;
    fixture_step(&f);

    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
}

SP_TEST(heater_is_disabled_when_the_valve_is_not_open, "REQ-013")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 60.0f);

    fixture_set_valve_position(&f, 89.9f);
    fixture_step(&f);

    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
}

SP_TEST(heater_is_never_on_outside_the_running_state, "REQ-013")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_set_temperature(&f, 30.0f);
    for (int i = 0; i < 100; i++) {
        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
        SP_ASSERT_NEAR(0.0, f.outputs.heater_power_pct, 0);
    }
}

/* Setpoint: accepted range 40.0 .. 90.0 degC */

static float setpoint_after_request(float requested)
{
    BoilerFixture f;
    BoilerCommands command = {.setpoint_c = requested, .set_setpoint = 1};

    fixture_init(&f);
    fixture_reach_standby(&f);
    fixture_command(&f, command);
    return f.status.setpoint_c;
}

SP_TEST(setpoint_defaults_to_80, "REQ-005")
{
    BoilerFixture f;
    fixture_init(&f);

    fixture_step(&f);

    SP_ASSERT_NEAR(80.0, f.status.setpoint_c, 0);
}

SP_TEST(setpoint_accepts_the_range_limits, "REQ-005")
{
    SP_ASSERT_NEAR(40.0, setpoint_after_request(40.0f), 0);
    SP_ASSERT_NEAR(90.0, setpoint_after_request(90.0f), 0);
}

SP_TEST(setpoint_rejects_values_outside_the_range, "REQ-005")
{
    SP_ASSERT_NEAR(80.0, setpoint_after_request(39.9f), 0);
    SP_ASSERT_NEAR(80.0, setpoint_after_request(90.1f), 0);
    SP_ASSERT_NEAR(80.0, setpoint_after_request(NAN), 0);
}

SP_TEST(setpoint_value_is_ignored_without_the_apply_flag, "REQ-005")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);
    const BoilerCommands command = {.setpoint_c = 60.0f};

    fixture_command(&f, command);

    SP_ASSERT_NEAR(80.0, f.status.setpoint_c, 0);
}

/* Shutdown */

SP_TEST(shutdown_circulates_until_the_boiler_has_cooled, "REQ-017")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 80.0f);
    const BoilerCommands stop = {.stop = 1};

    fixture_command(&f, stop);
    fixture_step_following(&f, 1000);

    SP_ASSERT_EQ_INT(BOILER_STATE_SHUTDOWN, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
    SP_ASSERT_EQ_INT(1, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(1, f.outputs.valve_open);
}

SP_TEST(shutdown_ends_at_the_cooldown_temperature, "REQ-017")
{
    BoilerFixture f = running_fixture();
    const BoilerCommands stop = {.stop = 1};
    fixture_command(&f, stop);

    fixture_set_temperature(&f, 55.1f);
    fixture_step_following(&f, 500);
    SP_ASSERT_EQ_INT(BOILER_STATE_SHUTDOWN, f.status.state);

    fixture_set_temperature(&f, 55.0f);
    fixture_step_following(&f, 200);
    SP_ASSERT_EQ_INT(BOILER_STATE_STANDBY, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.pump_run);
}

SP_TEST(start_is_ignored_during_shutdown, "REQ-017")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 80.0f);
    const BoilerCommands stop = {.stop = 1};
    const BoilerCommands start = {.start = 1};
    fixture_command(&f, stop);

    fixture_command(&f, start);

    SP_ASSERT_EQ_INT(BOILER_STATE_SHUTDOWN, f.status.state);
}

/* Fault state */

SP_TEST(fault_state_keeps_circulation_running_after_an_over_temperature_trip, "REQ-016")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 111.0f);

    fixture_step_following(&f, 500);

    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
    SP_ASSERT_EQ_INT(1, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(1, f.outputs.valve_open);
}

SP_TEST(fault_state_stops_the_pump_once_the_boiler_has_cooled, "REQ-016")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 111.0f);
    fixture_step_following(&f, 300);

    fixture_set_temperature(&f, 55.0f);
    fixture_step_following(&f, 300);

    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.pump_run);
}

SP_TEST(fault_state_does_not_run_the_pump_against_an_over_pressure, "REQ-016")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 80.0f);
    fixture_set_pressure(&f, 4.5f);

    fixture_step_following(&f, 300);

    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(1, f.outputs.valve_open);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
}

SP_TEST(fault_state_does_not_circulate_when_flow_is_lost, "REQ-016")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 80.0f);
    fixture_set_flow(&f, 0.0f);

    for (int i = 0; i < 40; i++) {
        fixture_step(&f);
        fixture_set_flow(&f, 0.0f);
    }

    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.pump_run);
}

SP_TEST(fault_state_circulates_when_the_temperature_is_unknown, "REQ-016")
{
    BoilerFixture f = running_fixture();
    f.inputs.temperature_ma = 0.0f;

    fixture_step_following(&f, 1000);

    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
    SP_ASSERT_EQ_INT(1, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
}

SP_TEST(reset_is_refused_while_the_fault_condition_persists, "REQ-015")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 111.0f);
    fixture_step_following(&f, 300);
    const BoilerCommands reset = {.reset = 1};

    fixture_command(&f, reset);

    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
}

SP_TEST(reset_returns_to_standby_once_the_condition_has_cleared, "REQ-015")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 111.0f);
    fixture_step_following(&f, 300);
    fixture_set_temperature(&f, 60.0f);
    fixture_step_following(&f, 300);
    const BoilerCommands reset = {.reset = 1};

    fixture_command(&f, reset);

    SP_ASSERT_EQ_INT(BOILER_STATE_STANDBY, f.status.state);
    SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
}

SP_TEST(fault_state_is_left_only_by_reset, "REQ-015")
{
    BoilerFixture f = running_fixture();
    fixture_set_temperature(&f, 111.0f);
    fixture_step_following(&f, 300);
    fixture_set_temperature(&f, 60.0f);
    const BoilerCommands start = {.start = 1};

    fixture_command(&f, start);
    fixture_step_following(&f, 5000);

    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
}

SP_TEST(reset_outside_the_fault_state_has_no_effect, "REQ-015")
{
    BoilerFixture f = running_fixture();
    const BoilerCommands reset = {.reset = 1};

    fixture_command(&f, reset);

    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
}

/* Manual pump/valve control */

SP_TEST(manual_pump_and_valve_work_in_standby, "REQ-018")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);
    const BoilerCommands both_on = {.pump_manual = 1, .valve_manual = 1};
    const BoilerCommands pump_off = {.pump_manual = 2};

    fixture_command(&f, both_on);
    SP_ASSERT_EQ_INT(1, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(1, f.outputs.valve_open);

    fixture_command(&f, pump_off);
    SP_ASSERT_EQ_INT(0, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(1, f.outputs.valve_open);
}

SP_TEST(manual_requests_are_ignored_while_running, "REQ-018")
{
    BoilerFixture f = running_fixture();
    const BoilerCommands pump_off = {.pump_manual = 2, .valve_manual = 2};

    fixture_command(&f, pump_off);

    SP_ASSERT_EQ_INT(1, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(1, f.outputs.valve_open);
}

SP_TEST(manual_selection_is_cleared_when_leaving_standby, "REQ-018")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);
    const BoilerCommands pump_on = {.pump_manual = 1};
    const BoilerCommands start = {.start = 1};
    const BoilerCommands stop = {.stop = 1};
    fixture_command(&f, pump_on);
    fixture_command(&f, start);
    fixture_command(&f, stop);
    fixture_set_temperature(&f, 50.0f);

    fixture_step_following(&f, 500);

    SP_ASSERT_EQ_INT(BOILER_STATE_STANDBY, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.pump_run);
    SP_ASSERT_EQ_INT(0, f.outputs.valve_open);
}

/* Alarm handling */

SP_TEST(horn_sounds_for_an_unacknowledged_critical_alarm, "REQ-019")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);

    fixture_set_pressure(&f, 4.5f);
    fixture_step(&f);

    SP_ASSERT_EQ_INT(1, f.outputs.alarm_horn);
}

SP_TEST(acknowledge_silences_the_horn_but_keeps_the_alarm, "REQ-019")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);
    fixture_set_pressure(&f, 4.5f);
    fixture_step(&f);
    const BoilerCommands ack = {.ack = 1};

    fixture_command(&f, ack);

    SP_ASSERT_EQ_INT(0, f.outputs.alarm_horn);
    SP_ASSERT_EQ_INT(0, f.status.alarms_unacked);
    SP_ASSERT((f.status.alarms_active & BOILER_FAULT_BIT(BOILER_FAULT_OVER_PRESSURE)) != 0);
}

SP_TEST(warnings_do_not_sound_the_horn, "REQ-019")
{
    BoilerFixture f;
    fixture_init(&f);
    fixture_reach_standby(&f);

    fixture_set_temperature(&f, 105.0f);
    fixture_run_ms(&f, 2000);

    SP_ASSERT((f.status.alarms_unacked & BOILER_FAULT_BIT(BOILER_FAULT_TEMP_HIGH)) != 0);
    SP_ASSERT_EQ_INT(0, f.outputs.alarm_horn);
}

/* Determinism */

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static float random_between(uint32_t *state, float low, float high)
{
    return low + (high - low) * ((float)(next_random(state) & 0xFFFF) / 65535.0f);
}

SP_TEST(identical_input_sequences_produce_identical_results, "REQ-020")
{
    BoilerFixture a;
    BoilerFixture b;
    uint32_t seed = 12345;

    fixture_init(&a);
    fixture_init(&b);
    for (int i = 0; i < 3000; i++) {
        BoilerCommands commands = {0};
        commands.start = (next_random(&seed) % 200) == 0;
        commands.stop = (next_random(&seed) % 400) == 0;
        commands.reset = (next_random(&seed) % 100) == 0;
        a.inputs.temperature_ma = random_between(&seed, 3.0f, 22.0f);
        a.inputs.pressure_ma = random_between(&seed, 3.0f, 22.0f);
        a.inputs.flow_ma = random_between(&seed, 3.0f, 22.0f);
        a.inputs.valve_position_ma = random_between(&seed, 3.0f, 22.0f);
        a.inputs.pump_running = next_random(&seed) & 1;
        b.inputs = a.inputs;

        fixture_step_with(&a, &commands);
        fixture_step_with(&b, &commands);

        SP_ASSERT(memcmp(&a.outputs, &b.outputs, sizeof a.outputs) == 0);
        SP_ASSERT(memcmp(&a.status, &b.status, sizeof a.status) == 0);
    }
}

SP_TEST(zero_length_step_changes_nothing, "REQ-020")
{
    BoilerFixture f = running_fixture();
    const BoilerStatus before = f.status;
    const BoilerCommands stop = {.stop = 1};
    BoilerOutputs outputs;

    boiler_step(&f.controller, &f.inputs, &stop, 0, &outputs);
    boiler_get_status(&f.controller, &f.status);

    SP_ASSERT(memcmp(&before, &f.status, sizeof before) == 0);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
}

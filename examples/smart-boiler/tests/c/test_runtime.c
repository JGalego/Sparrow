#include "boiler_commands.h"
#include "boiler_io.h"
#include "sp_test.h"

SP_TEST(repeated_start_is_one_start, "REQ-022")
{
    BoilerCommands pending = {0};
    const BoilerCommands start = {.start = 1};

    boiler_commands_merge(&pending, &start);
    boiler_commands_merge(&pending, &start);

    SP_ASSERT_EQ_INT(1, pending.start);
}

SP_TEST(requests_from_several_frames_are_combined, "REQ-022")
{
    BoilerCommands pending = {0};
    const BoilerCommands ack = {.ack = 1};
    const BoilerCommands reset = {.reset = 1};

    boiler_commands_merge(&pending, &ack);
    boiler_commands_merge(&pending, &reset);

    SP_ASSERT_EQ_INT(1, pending.ack);
    SP_ASSERT_EQ_INT(1, pending.reset);
}

SP_TEST(later_setpoint_replaces_an_earlier_one, "REQ-022")
{
    BoilerCommands pending = {0};
    const BoilerCommands first = {.set_setpoint = 1, .setpoint_c = 60.0f};
    const BoilerCommands second = {.set_setpoint = 1, .setpoint_c = 70.0f};
    const BoilerCommands unrelated = {.ack = 1, .setpoint_c = 99.0f};

    boiler_commands_merge(&pending, &first);
    boiler_commands_merge(&pending, &second);
    boiler_commands_merge(&pending, &unrelated);

    SP_ASSERT_NEAR(70.0, pending.setpoint_c, 0);
}

SP_TEST(stop_cancels_a_start_in_the_same_window_in_either_order, "REQ-022")
{
    BoilerCommands pending = {0};
    const BoilerCommands start = {.start = 1};
    const BoilerCommands stop = {.stop = 1};

    boiler_commands_merge(&pending, &start);
    boiler_commands_merge(&pending, &stop);
    SP_ASSERT_EQ_INT(0, pending.start);

    pending = (BoilerCommands){0};
    boiler_commands_merge(&pending, &stop);
    boiler_commands_merge(&pending, &start);
    SP_ASSERT_EQ_INT(0, pending.start);
    SP_ASSERT_EQ_INT(1, pending.stop);
}

SP_TEST(manual_request_keeps_the_latest_non_zero_value, "REQ-022")
{
    BoilerCommands pending = {0};
    const BoilerCommands on = {.pump_manual = 1};
    const BoilerCommands none = {0};

    boiler_commands_merge(&pending, &on);
    boiler_commands_merge(&pending, &none);

    SP_ASSERT_EQ_INT(1, pending.pump_manual);
}

SP_TEST(lost_io_reads_as_open_loops, "REQ-023")
{
    const BoilerInputs inputs = boiler_io_open_loop();

    SP_ASSERT_NEAR(0.0, inputs.temperature_ma, 0);
    SP_ASSERT_NEAR(0.0, inputs.pressure_ma, 0);
    SP_ASSERT_NEAR(0.0, inputs.flow_ma, 0);
    SP_ASSERT_NEAR(0.0, inputs.valve_position_ma, 0);
    SP_ASSERT_EQ_INT(0, inputs.pump_running);
}

SP_TEST(safe_outputs_de_energize_every_actuator, "REQ-023")
{
    const BoilerOutputs outputs = boiler_io_safe_outputs();

    SP_ASSERT_EQ_INT(0, outputs.heater_contactor);
    SP_ASSERT_NEAR(0.0, outputs.heater_power_pct, 0);
    SP_ASSERT_EQ_INT(0, outputs.pump_run);
    SP_ASSERT_EQ_INT(0, outputs.valve_open);
    SP_ASSERT_EQ_INT(0, outputs.alarm_horn);
}

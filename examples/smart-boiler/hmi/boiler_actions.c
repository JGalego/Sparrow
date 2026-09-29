#include "boiler_actions.h"

#include "sp_util.h"

BoilerCommands boiler_action_start(void)
{
    BoilerCommands commands = {0};

    commands.start = 1;
    return commands;
}

BoilerCommands boiler_action_stop(void)
{
    BoilerCommands commands = {0};

    commands.stop = 1;
    return commands;
}

BoilerCommands boiler_action_acknowledge(void)
{
    BoilerCommands commands = {0};

    commands.ack = 1;
    return commands;
}

BoilerCommands boiler_action_reset(void)
{
    BoilerCommands commands = {0};

    commands.reset = 1;
    return commands;
}

BoilerCommands boiler_action_toggle_pump(const BoilerApp *app)
{
    BoilerCommands commands = {0};

    commands.pump_manual = app->status.pump_on ? 2 : 1;
    return commands;
}

BoilerCommands boiler_action_toggle_valve(const BoilerApp *app)
{
    BoilerCommands commands = {0};

    commands.valve_manual = app->status.valve_open ? 2 : 1;
    return commands;
}

BoilerCommands boiler_action_change_setpoint(const BoilerApp *app, float delta_c)
{
    const BoilerConfig config = boiler_config_default();
    BoilerCommands commands = {0};

    commands.set_setpoint = 1;
    commands.setpoint_c =
        sp_clampf(app->status.setpoint_c + delta_c, config.setpoint_min_c, config.setpoint_max_c);
    return commands;
}

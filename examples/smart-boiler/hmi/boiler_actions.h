#ifndef BOILER_ACTIONS_H
#define BOILER_ACTIONS_H

#include "boiler_app.h"

/* Operator actions as controller commands. The controller decides whether to act on them. */

BoilerCommands boiler_action_start(void);
BoilerCommands boiler_action_stop(void);
BoilerCommands boiler_action_acknowledge(void);
BoilerCommands boiler_action_reset(void);

/* Requests the opposite of the current pump/valve command. */
BoilerCommands boiler_action_toggle_pump(const BoilerApp *app);
BoilerCommands boiler_action_toggle_valve(const BoilerApp *app);

/* Requests the current setpoint plus delta_c, limited to the configured setpoint range. */
BoilerCommands boiler_action_change_setpoint(const BoilerApp *app, float delta_c);

#endif

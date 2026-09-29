#ifndef BOILER_CONTROLLER_H
#define BOILER_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "boiler_faults.h"
#include "boiler_measure.h"
#include "boiler_model.h"
#include "sp_alarm.h"
#include "sp_persist.h"
#include "sp_pi.h"
#include "sp_status.h"

/*
 * Boiler controller state. The caller allocates it; the controller never
 * allocates memory and never reads a clock. Time advances only through the
 * dt_ms argument of boiler_step(), so a run is fully determined by its inputs.
 * Fields are exposed for tests and diagnostics; treat them as read-only.
 */
typedef struct {
    BoilerConfig config;
    BoilerState state;
    uint32_t state_time_ms;
    uint32_t uptime_ms;
    float setpoint_c;

    BoilerMeasurements measurements;
    SpAlarmSet alarms;
    BoilerFaultTimers fault_timers;
    SpPersist flow_established;
    SpPi heater_pi;

    BoilerOutputs outputs;
    bool manual_pump_on;
    bool manual_valve_open;
    uint32_t pump_on_ms;
    uint32_t valve_command_ms;
} BoilerController;

/*
 * Validates the configuration and resets the controller to INIT with all
 * outputs off. Returns SP_ERR_RANGE with a description in *reason (may be
 * NULL) if the configuration is unusable; the controller is then untouched.
 */
SpStatus boiler_init(BoilerController *controller, const BoilerConfig *config, const char **reason);

/* Checks parameter ranges and the relations between limits. */
SpStatus boiler_config_validate(const BoilerConfig *config, const char **reason);

/*
 * Runs one control step of dt_ms milliseconds (dt_ms > 0) and writes the
 * actuator commands to *outputs. Commands are consumed by this step.
 */
void boiler_step(BoilerController *controller, const BoilerInputs *inputs,
                 const BoilerCommands *commands, uint32_t dt_ms, BoilerOutputs *outputs);

void boiler_get_status(const BoilerController *controller, BoilerStatus *status);

#endif

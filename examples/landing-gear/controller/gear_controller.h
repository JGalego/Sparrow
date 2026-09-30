#ifndef GEAR_CONTROLLER_H
#define GEAR_CONTROLLER_H

#include <stdint.h>

#include "gear_model.h"
#include "sp_alarm.h"
#include "sp_status.h"

/*
 * Landing gear controller state. The caller allocates it; the controller
 * never allocates memory and never reads a clock. Time advances only through
 * the dt_ms argument of gear_step(), so a run is fully determined by its
 * inputs. Fields are exposed for tests and diagnostics; treat them as
 * read-only.
 */
typedef struct {
    GearConfig config;
    GearState state;
    uint32_t state_time_ms;
    uint32_t uptime_ms;
    /* Independent of state time: inhibition must not pause an active transit. */
    uint32_t normal_transit_time_ms;
    uint8_t normal_transit_active;
    uint8_t normal_transit_handle_down;
    /* Valid altitude qualification retained until ground indication or reinitialization. */
    uint8_t gear_warning_armed;
    /* Retained only during a warning episode above the configured mute limit. */
    uint8_t horn_muted;
    SpAlarmSet alarms;
    GearOutputs outputs;
} GearController;

/*
 * Validates the configuration and resets the controller to INIT with all
 * outputs off. Returns SP_ERR_RANGE with a description in *reason (may be
 * NULL) if the configuration is unusable; the controller is then untouched.
 */
SpStatus gear_init(GearController *controller, const GearConfig *config, const char **reason);

/* Checks parameter ranges and the relations between limits. */
SpStatus gear_config_validate(const GearConfig *config, const char **reason);

/*
 * Runs one control step of dt_ms milliseconds (dt_ms > 0) and writes the
 * actuator commands to *outputs. Commands are consumed by this step.
 */
void gear_step(GearController *controller, const GearInputs *inputs, const GearCommands *commands,
               uint32_t dt_ms, GearOutputs *outputs);

void gear_get_status(const GearController *controller, GearStatus *status);

#endif

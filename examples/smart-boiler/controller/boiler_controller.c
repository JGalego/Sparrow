#include "boiler_controller.h"

#include <math.h>
#include <string.h>

#include "boiler_sequence.h"

static SpStatus fail(const char **reason, const char *text)
{
    if (reason != NULL) {
        *reason = text;
    }
    return SP_ERR_RANGE;
}

SpStatus boiler_config_validate(const BoilerConfig *config, const char **reason)
{
    const int bad = boiler_config_check_ranges(config);

    if (bad >= 0) {
        return fail(reason, boiler_config_param_name(bad));
    }
    if (!(config->temperature_max_c > config->temperature_min_c)) {
        return fail(reason, "temperature_max_c must exceed temperature_min_c");
    }
    if (!(config->temperature_trip_c < config->temperature_max_c)) {
        return fail(reason, "temperature_trip_c must lie inside the sensor span");
    }
    if (!(config->pressure_trip_bar < config->pressure_max_bar)) {
        return fail(reason, "pressure_trip_bar must lie inside the sensor span");
    }
    if (!(config->temperature_warn_c < config->temperature_trip_c)) {
        return fail(reason, "temperature_warn_c must be below temperature_trip_c");
    }
    if (!(config->setpoint_max_c < config->temperature_warn_c)) {
        return fail(reason, "setpoint_max_c must be below temperature_warn_c");
    }
    if (!(config->setpoint_min_c <= config->setpoint_default_c &&
          config->setpoint_default_c <= config->setpoint_max_c)) {
        return fail(reason, "setpoint_default_c must lie within the setpoint limits");
    }
    if (!(config->pressure_low_bar < config->pressure_warn_bar &&
          config->pressure_warn_bar < config->pressure_trip_bar)) {
        return fail(reason, "pressure limits must satisfy low < warn < trip");
    }
    if (!(config->valve_closed_pct < config->valve_open_pct)) {
        return fail(reason, "valve_closed_pct must be below valve_open_pct");
    }
    return SP_OK;
}

SpStatus boiler_init(BoilerController *controller, const BoilerConfig *config, const char **reason)
{
    const SpStatus status = boiler_config_validate(config, reason);

    if (status != SP_OK) {
        return status;
    }
    memset(controller, 0, sizeof *controller);
    controller->config = *config;
    controller->state = BOILER_STATE_INIT;
    controller->setpoint_c = config->setpoint_default_c;
    sp_alarms_init(&controller->alarms);
    boiler_faults_init(&controller->fault_timers);
    sp_pi_init(&controller->heater_pi, config->heater_kp, config->heater_ki, 0.0f, 100.0f);
    return SP_OK;
}

static uint32_t saturating_add(uint32_t value, uint32_t amount)
{
    return value > UINT32_MAX - amount ? UINT32_MAX : value + amount;
}

static BoilerCommanded commanded_view(const BoilerController *c)
{
    const BoilerCommanded commanded = {
        .pump_commanded = c->outputs.pump_run != 0,
        .pump_on_ms = c->pump_on_ms,
        .valve_commanded_open = c->outputs.valve_open != 0,
        .valve_command_ms = c->valve_command_ms,
    };
    return commanded;
}

static void advance_command_timers(BoilerController *c, uint32_t dt_ms, const BoilerOutputs *before)
{
    c->pump_on_ms = c->outputs.pump_run ? saturating_add(c->pump_on_ms, dt_ms) : 0;
    if (c->outputs.valve_open != before->valve_open) {
        c->valve_command_ms = 0;
    } else {
        c->valve_command_ms = saturating_add(c->valve_command_ms, dt_ms);
    }
    c->uptime_ms = saturating_add(c->uptime_ms, dt_ms);
    c->state_time_ms = saturating_add(c->state_time_ms, dt_ms);
}

static void update_heatup(BoilerController *c, BoilerState previous_state, uint32_t dt_ms)
{
    const BoilerReading *temperature = &c->measurements.temperature_c;

    if (c->state != BOILER_STATE_STARTUP && c->state != BOILER_STATE_RUNNING) {
        c->heatup_monitoring = false;
        c->heatup_elapsed_ms = 0;
    } else if (previous_state == BOILER_STATE_STANDBY && c->state == BOILER_STATE_STARTUP) {
        /* The accepting step ends at the transition; no earlier time belongs to heat-up. */
        c->heatup_monitoring = true;
        c->heatup_elapsed_ms = 0;
    } else if (c->heatup_monitoring) {
        const uint64_t elapsed = c->heatup_elapsed_ms;

        c->heatup_elapsed_ms = elapsed > UINT64_MAX - dt_ms ? UINT64_MAX : elapsed + dt_ms;
    }

    /* Completion uses the newly applied setpoint and takes precedence over timeout. */
    if (c->heatup_monitoring && temperature->valid && temperature->value >= c->setpoint_c) {
        c->heatup_monitoring = false;
    }
    const bool timed_out =
        c->heatup_monitoring && c->heatup_elapsed_ms > c->config.heatup_timeout_ms;

    sp_alarms_update(&c->alarms, BOILER_FAULT_HEATUP_TIMEOUT, timed_out, false);
}

static uint8_t horn_required(const BoilerController *c)
{
    return (c->alarms.unacked & c->alarms.active & BOILER_CRITICAL_FAULT_MASK) != 0;
}

void boiler_step(BoilerController *c, const BoilerInputs *inputs, const BoilerCommands *commands,
                 uint32_t dt_ms, BoilerOutputs *outputs)
{
    if (dt_ms == 0) {
        *outputs = c->outputs;
        return;
    }
    const BoilerOutputs before = c->outputs;
    const BoilerCommanded commanded = commanded_view(c);
    const BoilerState previous_state = c->state;

    c->measurements = boiler_measure(&c->config, inputs);
    boiler_faults_update(&c->fault_timers, &c->alarms, &c->config, &c->measurements, &commanded,
                         dt_ms);
    if (commands->ack) {
        sp_alarms_ack_all(&c->alarms);
    }
    boiler_sequence_step(c, commands, dt_ms);
    update_heatup(c, previous_state, dt_ms);
    c->outputs.alarm_horn = horn_required(c);
    advance_command_timers(c, dt_ms, &before);
    *outputs = c->outputs;
}

void boiler_get_status(const BoilerController *c, BoilerStatus *status)
{
    const BoilerMeasurements *m = &c->measurements;

    memset(status, 0, sizeof *status);
    status->temperature_c = m->temperature_c.value;
    status->pressure_bar = m->pressure_bar.value;
    status->flow_lpm = m->flow_lpm.value;
    status->valve_position_pct = m->valve_position_pct.value;
    status->heater_power_pct = c->outputs.heater_power_pct;
    status->setpoint_c = c->setpoint_c;
    status->alarms_active = c->alarms.active;
    status->alarms_latched = c->alarms.latched;
    status->alarms_unacked = c->alarms.unacked;
    status->uptime_ms = c->uptime_ms;
    status->state_time_ms = c->state_time_ms;
    status->state = (uint8_t)c->state;
    status->pump_on = c->outputs.pump_run;
    status->valve_open = c->outputs.valve_open;
    status->heater_contactor = c->outputs.heater_contactor;
    status->temperature_valid = m->temperature_c.valid;
    status->pressure_valid = m->pressure_bar.valid;
    status->flow_valid = m->flow_lpm.valid;
    status->valve_valid = m->valve_position_pct.valid;
    status->alarm_horn = c->outputs.alarm_horn;
}

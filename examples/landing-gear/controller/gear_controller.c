#include "gear_controller.h"

#include <string.h>

SpStatus gear_config_validate(const GearConfig *config, const char **reason)
{
    const int bad = gear_config_check_ranges(config);

    if (bad >= 0) {
        if (reason != NULL) {
            *reason = gear_config_param_name(bad);
        }
        return SP_ERR_RANGE;
    }
    return SP_OK;
}

SpStatus gear_init(GearController *controller, const GearConfig *config, const char **reason)
{
    const SpStatus status = gear_config_validate(config, reason);

    if (status != SP_OK) {
        return status;
    }
    memset(controller, 0, sizeof *controller);
    controller->config = *config;
    controller->state = GEAR_STATE_INIT;
    sp_alarms_init(&controller->alarms);
    return SP_OK;
}

static uint32_t saturating_add(uint32_t value, uint32_t amount)
{
    return value > UINT32_MAX - amount ? UINT32_MAX : value + amount;
}

static int gear_down_locked(const GearInputs *inputs)
{
    return inputs->nose_downlock != 0 && inputs->left_downlock != 0 &&
           inputs->right_downlock != 0 && inputs->nose_uplock == 0 && inputs->left_uplock == 0 &&
           inputs->right_uplock == 0;
}

static int gear_up_locked(const GearInputs *inputs)
{
    return inputs->nose_uplock != 0 && inputs->left_uplock != 0 && inputs->right_uplock != 0 &&
           inputs->nose_downlock == 0 && inputs->left_downlock == 0 && inputs->right_downlock == 0;
}

static int gear_retraction_permitted(const GearConfig *config, const GearInputs *inputs)
{
    return inputs->left_wow == 0 && inputs->right_wow == 0 && inputs->airspeed_valid != 0 &&
           inputs->airspeed_kt >= config->min_retraction_airspeed_kt;
}

static void gear_set_hydraulic_selector(GearOutputs *outputs, GearState operation)
{
    /* Break before make: clear both selectors before energizing either one. */
    outputs->down_valve = 0;
    outputs->up_valve = 0;
    if (operation == GEAR_STATE_EXTENDING) {
        outputs->down_valve = 1;
    } else if (operation == GEAR_STATE_RETRACTING) {
        outputs->up_valve = 1;
    }
}

static GearState gear_select_normal_extension(const GearInputs *inputs, GearOutputs *outputs)
{
    const GearState operation =
        gear_down_locked(inputs) ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_EXTENDING;

    gear_set_hydraulic_selector(outputs, operation);
    return operation;
}

static GearState gear_select_normal_retraction(const GearInputs *inputs, GearOutputs *outputs)
{
    const GearState operation =
        gear_up_locked(inputs) ? GEAR_STATE_UP_LOCKED : GEAR_STATE_RETRACTING;

    gear_set_hydraulic_selector(outputs, operation);
    return operation;
}

static GearState gear_select_alternate_extension(GearOutputs *outputs)
{
    /* Free-fall extension must never oppose either hydraulic selector. */
    gear_set_hydraulic_selector(outputs, GEAR_STATE_ALTERNATE_EXTENDING);
    outputs->uplock_release = 1;
    return GEAR_STATE_ALTERNATE_EXTENDING;
}

static GearState gear_select_operation(const GearInputs *inputs, GearOutputs *outputs)
{
    /* Apply alternate extension before considering any normal lever operation. */
    if (inputs->alternate_extend != 0) {
        return gear_select_alternate_extension(outputs);
    }
    outputs->uplock_release = 0;
    if (inputs->gear_handle_down != 0) {
        return gear_select_normal_extension(inputs, outputs);
    }
    return gear_select_normal_retraction(inputs, outputs);
}

static void gear_update_normal_transit(GearController *c, const GearInputs *inputs, uint32_t dt_ms)
{
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint8_t handle_down = inputs->gear_handle_down != 0 ? 1 : 0;
    const int complete = handle_down != 0 ? gear_down_locked(inputs) : gear_up_locked(inputs);
    int timed_out = 0;

    /* Completion and cancellation take precedence over elapsed-time evaluation. */
    if (inputs->alternate_extend != 0 || complete ||
        (c->normal_transit_active != 0 && c->normal_transit_handle_down != handle_down)) {
        c->normal_transit_active = 0;
        c->normal_transit_time_ms = 0;
    }
    if (inputs->alternate_extend == 0 && !complete) {
        if (c->normal_transit_active != 0) {
            c->normal_transit_time_ms = saturating_add(c->normal_transit_time_ms, dt_ms);
        } else if (c->outputs.down_valve != 0 || c->outputs.up_valve != 0) {
            /* Start at this command, not during the interval preceding it. */
            c->normal_transit_active = 1;
            c->normal_transit_handle_down = handle_down;
            c->normal_transit_time_ms = 0;
        }
        timed_out = c->normal_transit_active != 0 &&
                    c->normal_transit_time_ms >= c->config.normal_transit_timeout_ms;
    }
    if (timed_out) {
        if ((c->alarms.active & fault) == 0) {
            c->alarms.unacked |= fault;
        }
        c->alarms.active |= fault;
        c->alarms.latched |= fault;
    } else {
        /* Removing the condition never clears an existing fault latch. */
        c->alarms.active &= ~fault;
    }
}

static void gear_reset_disagree(GearController *c, const GearInputs *inputs,
                                const GearCommands *commands)
{
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);

    if (commands == NULL || commands->reset == 0) {
        return;
    }
    if ((inputs->gear_handle_down != 0 && gear_down_locked(inputs)) ||
        (inputs->gear_handle_down == 0 && gear_up_locked(inputs))) {
        /* An accepted reset clears only this fault, not unrelated alarms. */
        c->alarms.active &= ~fault;
        c->alarms.latched &= ~fault;
        c->alarms.unacked &= ~fault;
    }
}

static void gear_update_warning_arming(GearController *c, const GearInputs *inputs)
{
    /* Ground indication clears the qualification even at or above the altitude limit. */
    if (inputs->left_wow != 0 || inputs->right_wow != 0) {
        c->gear_warning_armed = 0;
    } else if (inputs->radio_altitude_valid != 0 &&
               inputs->radio_altitude_ft >= c->config.gear_warning_altitude_ft) {
        c->gear_warning_armed = 1;
    }
}

static void gear_update_too_low_warning(GearController *c, const GearInputs *inputs)
{
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const int condition = c->gear_warning_armed != 0 && inputs->radio_altitude_valid != 0 &&
                          inputs->airspeed_valid != 0 &&
                          inputs->radio_altitude_ft < c->config.gear_warning_altitude_ft &&
                          inputs->airspeed_kt < c->config.gear_warning_airspeed_kt &&
                          !gear_down_locked(inputs);

    if (condition) {
        if ((c->alarms.active & fault) == 0) {
            c->alarms.unacked |= fault;
        }
        c->alarms.active |= fault;
    } else {
        c->alarms.active &= ~fault;
    }
}

static void gear_update_horn(GearController *c, const GearInputs *inputs,
                             const GearCommands *commands)
{
    const int warning = (c->alarms.active & GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR)) != 0;

    /* End the warning episode immediately; an absent warning cannot accept a mute. */
    if (!warning) {
        c->horn_muted = 0;
        c->outputs.gear_horn = 0;
        return;
    }
    /* Low-altitude cancellation takes priority over a new mute, including at the limit. */
    if (inputs->radio_altitude_ft <= c->config.horn_mute_min_altitude_ft) {
        c->horn_muted = 0;
        c->outputs.gear_horn = 1;
        return;
    }
    /* A climb cannot restore a canceled mute; only a new request can set it. */
    if (commands != NULL && commands->mute != 0) {
        c->horn_muted = 1;
    }
    c->outputs.gear_horn = c->horn_muted == 0 ? 1 : 0;
}

void gear_step(GearController *c, const GearInputs *inputs, const GearCommands *commands,
               uint32_t dt_ms, GearOutputs *outputs)
{
    GearState next_state;

    if (dt_ms == 0) {
        *outputs = c->outputs;
        return;
    }
    c->uptime_ms = saturating_add(c->uptime_ms, dt_ms);
    c->outputs.handle_lock = (inputs->left_wow != 0 || inputs->right_wow != 0) ? 1 : 0;
    next_state = gear_select_operation(inputs, &c->outputs);
    /* Gate every selected operation, independent of state or fault latches. */
    if (!gear_retraction_permitted(&c->config, inputs)) {
        c->outputs.up_valve = 0;
    }
    gear_update_normal_transit(c, inputs, dt_ms);
    gear_reset_disagree(c, inputs, commands);
    gear_update_warning_arming(c, inputs);
    gear_update_too_low_warning(c, inputs);
    gear_update_horn(c, inputs, commands);
    if ((c->alarms.latched & GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE)) != 0) {
        next_state = GEAR_STATE_FAULT;
        c->outputs.master_warning = 1;
    } else {
        c->outputs.master_warning = 0;
    }
    if (next_state != c->state) {
        c->state = next_state;
        c->state_time_ms = 0;
    } else {
        c->state_time_ms = saturating_add(c->state_time_ms, dt_ms);
    }
    *outputs = c->outputs;
}

void gear_get_status(const GearController *c, GearStatus *status)
{
    memset(status, 0, sizeof *status);
    status->alarms_active = c->alarms.active;
    status->alarms_latched = c->alarms.latched;
    status->alarms_unacked = c->alarms.unacked;
    status->uptime_ms = c->uptime_ms;
    status->state_time_ms = c->state_time_ms;
    status->state = (uint8_t)c->state;
}

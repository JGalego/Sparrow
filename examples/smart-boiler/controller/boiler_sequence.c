#include "boiler_sequence.h"

#include "sp_util.h"

#define CIRCULATION_BLOCKING_FAULTS                                                                \
    (BOILER_FAULT_BIT(BOILER_FAULT_PUMP_FAILURE) | BOILER_FAULT_BIT(BOILER_FAULT_NO_FLOW) |        \
     BOILER_FAULT_BIT(BOILER_FAULT_VALVE_FAILURE) | BOILER_FAULT_BIT(BOILER_FAULT_OVER_PRESSURE) | \
     BOILER_FAULT_BIT(BOILER_FAULT_LOW_PRESSURE))

static void enter_state(BoilerController *c, BoilerState state)
{
    c->state = state;
    c->state_time_ms = 0;
    sp_pi_reset(&c->heater_pi);
    sp_persist_reset(&c->flow_established);
    c->manual_pump_on = false;
    c->manual_valve_open = false;
}

static void set_outputs(BoilerController *c, float heater_pct, bool contactor, bool pump,
                        bool valve)
{
    c->outputs.heater_power_pct = contactor ? heater_pct : 0.0f;
    c->outputs.heater_contactor = contactor ? 1 : 0;
    c->outputs.pump_run = pump ? 1 : 0;
    c->outputs.valve_open = valve ? 1 : 0;
}

static void apply_setpoint_command(BoilerController *c, const BoilerCommands *commands)
{
    const float requested = sp_quantize(commands->setpoint_c, 10.0f);

    if (commands->set_setpoint && requested >= c->config.setpoint_min_c &&
        requested <= c->config.setpoint_max_c) {
        c->setpoint_c = requested;
    }
}

static bool valve_proven_open(const BoilerController *c)
{
    const BoilerReading *position = &c->measurements.valve_position_pct;
    return position->valid && position->value >= c->config.valve_open_pct;
}

static bool flow_present(const BoilerController *c)
{
    const BoilerReading *flow = &c->measurements.flow_lpm;
    return flow->valid && flow->value >= c->config.flow_min_lpm;
}

static bool heater_permitted(const BoilerController *c)
{
    return c->measurements.temperature_c.valid && c->measurements.pump_running && flow_present(c) &&
           valve_proven_open(c);
}

static bool critical_fault_standing(const BoilerController *c)
{
    return (sp_alarms_standing(&c->alarms) & BOILER_CRITICAL_FAULT_MASK) != 0;
}

static void run_init(BoilerController *c)
{
    set_outputs(c, 0.0f, false, false, false);
    if (c->state_time_ms >= c->config.init_time_ms) {
        enter_state(c, BOILER_STATE_STANDBY);
    }
}

static void run_standby(BoilerController *c, const BoilerCommands *commands)
{
    if (commands->pump_manual != 0) {
        c->manual_pump_on = commands->pump_manual == 1;
    }
    if (commands->valve_manual != 0) {
        c->manual_valve_open = commands->valve_manual == 1;
    }
    set_outputs(c, 0.0f, false, c->manual_pump_on, c->manual_valve_open);
    if (commands->start) {
        enter_state(c, BOILER_STATE_STARTUP);
    }
}

/*
 * A stop takes effect in the step that accepts it: that step already drives
 * the SHUTDOWN outputs (heater off, circulation on). SHUTDOWN's own exit
 * condition is evaluated from the next step.
 */
static bool accept_stop(BoilerController *c, const BoilerCommands *commands)
{
    if (!commands->stop) {
        return false;
    }
    enter_state(c, BOILER_STATE_SHUTDOWN);
    set_outputs(c, 0.0f, false, true, true);
    return true;
}

static void run_startup(BoilerController *c, const BoilerCommands *commands, uint32_t dt_ms)
{
    if (accept_stop(c, commands)) {
        return;
    }
    const bool pump = valve_proven_open(c);
    const bool established = sp_persist_update(
        &c->flow_established, pump && c->measurements.pump_running && flow_present(c),
        c->config.flow_ok_delay_ms, dt_ms);

    set_outputs(c, 0.0f, false, pump, true);
    if (established) {
        enter_state(c, BOILER_STATE_RUNNING);
    }
}

static void run_running(BoilerController *c, const BoilerCommands *commands, uint32_t dt_ms)
{
    float heater_pct = 0.0f;
    const bool permitted = heater_permitted(c);

    if (accept_stop(c, commands)) {
        return;
    }
    if (permitted) {
        const float error = c->setpoint_c - c->measurements.temperature_c.value;
        heater_pct = sp_pi_step(&c->heater_pi, error, (float)dt_ms / 1000.0f);
    } else {
        sp_pi_reset(&c->heater_pi);
    }
    set_outputs(c, heater_pct, permitted, true, true);
}

static void run_shutdown(BoilerController *c)
{
    const BoilerReading *temperature = &c->measurements.temperature_c;

    set_outputs(c, 0.0f, false, true, true);
    if (temperature->valid && temperature->value <= c->config.cooldown_c) {
        enter_state(c, BOILER_STATE_STANDBY);
    }
}

static void run_fault(BoilerController *c, const BoilerCommands *commands)
{
    const BoilerReading *temperature = &c->measurements.temperature_c;
    const bool circulation_allowed =
        (sp_alarms_standing(&c->alarms) & CIRCULATION_BLOCKING_FAULTS) == 0;
    const bool cooling_needed = !temperature->valid || temperature->value > c->config.cooldown_c;

    set_outputs(c, 0.0f, false, circulation_allowed && cooling_needed, true);
    if (commands->reset && sp_alarms_reset(&c->alarms)) {
        enter_state(c, BOILER_STATE_STANDBY);
    }
}

void boiler_sequence_step(BoilerController *c, const BoilerCommands *commands, uint32_t dt_ms)
{
    apply_setpoint_command(c, commands);
    if (c->state != BOILER_STATE_FAULT && critical_fault_standing(c)) {
        enter_state(c, BOILER_STATE_FAULT);
    }

    switch (c->state) {
    case BOILER_STATE_INIT:
        run_init(c);
        break;
    case BOILER_STATE_STANDBY:
        run_standby(c, commands);
        break;
    case BOILER_STATE_STARTUP:
        run_startup(c, commands, dt_ms);
        break;
    case BOILER_STATE_RUNNING:
        run_running(c, commands, dt_ms);
        break;
    case BOILER_STATE_SHUTDOWN:
        run_shutdown(c);
        break;
    case BOILER_STATE_FAULT:
    case BOILER_STATE_COUNT:
        run_fault(c, commands);
        break;
    }
}

#include "boiler_faults.h"

#include "sp_util.h"

static bool is_active(const SpAlarmSet *alarms, BoilerFault fault)
{
    return (alarms->active & BOILER_FAULT_BIT(fault)) != 0;
}

static void update_sensor_faults(BoilerFaultTimers *timers, SpAlarmSet *alarms,
                                 const BoilerConfig *config, const BoilerMeasurements *m,
                                 uint32_t dt_ms)
{
    static const BoilerFault faults[4] = {BOILER_FAULT_TEMP_SENSOR, BOILER_FAULT_PRESSURE_SENSOR,
                                          BOILER_FAULT_FLOW_SENSOR, BOILER_FAULT_VALVE_SENSOR};
    const bool valid[4] = {m->temperature_c.valid, m->pressure_bar.valid, m->flow_lpm.valid,
                           m->valve_position_pct.valid};

    for (int i = 0; i < 4; i++) {
        const bool confirmed =
            sp_persist_update(&timers->sensor[i], !valid[i], config->sensor_fault_delay_ms, dt_ms);
        sp_alarms_update(alarms, faults[i], confirmed, true);
    }
}

static void update_temperature_faults(BoilerFaultTimers *timers, SpAlarmSet *alarms,
                                      const BoilerConfig *config, const BoilerReading *temperature,
                                      uint32_t dt_ms)
{
    bool trip = is_active(alarms, BOILER_FAULT_OVER_TEMP);
    bool warn = is_active(alarms, BOILER_FAULT_TEMP_HIGH);

    if (temperature->valid) {
        trip = sp_high_with_hysteresis(trip, temperature->value, config->temperature_trip_c,
                                       config->temperature_hysteresis_c);
        warn = sp_high_with_hysteresis(warn, temperature->value, config->temperature_warn_c,
                                       config->temperature_hysteresis_c);
        warn =
            sp_persist_update(&timers->temperature_warning, warn, config->warning_delay_ms, dt_ms);
    }
    sp_alarms_update(alarms, BOILER_FAULT_OVER_TEMP, trip, true);
    sp_alarms_update(alarms, BOILER_FAULT_TEMP_HIGH, warn, false);
}

static void update_pressure_faults(BoilerFaultTimers *timers, SpAlarmSet *alarms,
                                   const BoilerConfig *config, const BoilerReading *pressure,
                                   uint32_t dt_ms)
{
    bool trip = is_active(alarms, BOILER_FAULT_OVER_PRESSURE);
    bool warn = is_active(alarms, BOILER_FAULT_PRESSURE_HIGH);
    bool low = is_active(alarms, BOILER_FAULT_LOW_PRESSURE);

    if (pressure->valid) {
        const float hysteresis = config->pressure_hysteresis_bar;
        trip =
            sp_high_with_hysteresis(trip, pressure->value, config->pressure_trip_bar, hysteresis);
        warn =
            sp_high_with_hysteresis(warn, pressure->value, config->pressure_warn_bar, hysteresis);
        warn = sp_persist_update(&timers->pressure_warning, warn, config->warning_delay_ms, dt_ms);
        low = sp_low_with_hysteresis(low, pressure->value, config->pressure_low_bar, hysteresis);
        low = sp_persist_update(&timers->pressure_low, low, config->pressure_low_delay_ms, dt_ms);
    }
    sp_alarms_update(alarms, BOILER_FAULT_OVER_PRESSURE, trip, true);
    sp_alarms_update(alarms, BOILER_FAULT_PRESSURE_HIGH, warn, false);
    sp_alarms_update(alarms, BOILER_FAULT_LOW_PRESSURE, low, true);
}

static void update_pump_faults(BoilerFaultTimers *timers, SpAlarmSet *alarms,
                               const BoilerConfig *config, const BoilerMeasurements *m,
                               const BoilerCommanded *commanded, uint32_t dt_ms)
{
    const bool mismatch = m->pump_running != commanded->pump_commanded;
    const bool pump_failure =
        sp_persist_update(&timers->pump_mismatch, mismatch, config->pump_feedback_delay_ms, dt_ms);

    const bool flow_expected =
        commanded->pump_commanded && commanded->pump_on_ms >= config->flow_proof_delay_ms;
    const bool flow_missing =
        flow_expected && m->flow_lpm.valid && m->flow_lpm.value < config->flow_min_lpm;
    const bool no_flow =
        sp_persist_update(&timers->no_flow, flow_missing, config->flow_loss_delay_ms, dt_ms);

    sp_alarms_update(alarms, BOILER_FAULT_PUMP_FAILURE, pump_failure, true);
    sp_alarms_update(alarms, BOILER_FAULT_NO_FLOW, no_flow, true);
}

static void update_valve_fault(SpAlarmSet *alarms, const BoilerConfig *config,
                               const BoilerReading *position, const BoilerCommanded *commanded)
{
    const bool settled = commanded->valve_command_ms >= config->valve_travel_timeout_ms;
    bool failure = false;

    if (settled && position->valid) {
        failure = commanded->valve_commanded_open ? position->value < config->valve_open_pct
                                                  : position->value > config->valve_closed_pct;
    }
    sp_alarms_update(alarms, BOILER_FAULT_VALVE_FAILURE, failure, true);
}

void boiler_faults_init(BoilerFaultTimers *timers)
{
    *timers = (BoilerFaultTimers){0};
}

void boiler_faults_update(BoilerFaultTimers *timers, SpAlarmSet *alarms, const BoilerConfig *config,
                          const BoilerMeasurements *measurements, const BoilerCommanded *commanded,
                          uint32_t dt_ms)
{
    update_sensor_faults(timers, alarms, config, measurements, dt_ms);
    update_temperature_faults(timers, alarms, config, &measurements->temperature_c, dt_ms);
    update_pressure_faults(timers, alarms, config, &measurements->pressure_bar, dt_ms);
    update_pump_faults(timers, alarms, config, measurements, commanded, dt_ms);
    update_valve_fault(alarms, config, &measurements->valve_position_pct, commanded);
}

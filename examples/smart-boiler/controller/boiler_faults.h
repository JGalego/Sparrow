#ifndef BOILER_FAULTS_H
#define BOILER_FAULTS_H

#include <stdbool.h>
#include <stdint.h>

#include "boiler_measure.h"
#include "boiler_model.h"
#include "sp_alarm.h"
#include "sp_persist.h"

typedef struct {
    SpPersist sensor[4]; /* temperature, pressure, flow, valve position */
    SpPersist temperature_warning;
    SpPersist pressure_warning;
    SpPersist pressure_low;
    SpPersist pump_mismatch;
    SpPersist no_flow;
} BoilerFaultTimers;

/* What the sequencer is currently commanding, for feedback supervision. */
typedef struct {
    bool pump_commanded;
    uint32_t pump_on_ms;
    bool valve_commanded_open;
    uint32_t valve_command_ms;
} BoilerCommanded;

void boiler_faults_init(BoilerFaultTimers *timers);

/* Evaluates every fault condition for one control step and updates the alarm set. */
void boiler_faults_update(BoilerFaultTimers *timers, SpAlarmSet *alarms, const BoilerConfig *config,
                          const BoilerMeasurements *measurements, const BoilerCommanded *commanded,
                          uint32_t dt_ms);

#endif

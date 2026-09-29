#ifndef BOILER_MEASURE_H
#define BOILER_MEASURE_H

#include <stdbool.h>

#include "boiler_model.h"

typedef struct {
    float value;
    bool valid;
} BoilerReading;

typedef struct {
    BoilerReading temperature_c;
    BoilerReading pressure_bar;
    BoilerReading flow_lpm;
    BoilerReading valve_position_pct;
    bool pump_running;
} BoilerMeasurements;

/*
 * Converts the 4-20 mA loops to engineering units. A reading is invalid when
 * its loop current is outside [loop_fault_low_ma, loop_fault_high_ma] or not a
 * number. Valid readings are rounded to the transmitter resolution
 * (0.1 degC, 0.01 bar, 0.1 L/min, 0.1 %) so limit comparisons are exact.
 */
BoilerMeasurements boiler_measure(const BoilerConfig *config, const BoilerInputs *inputs);

#endif

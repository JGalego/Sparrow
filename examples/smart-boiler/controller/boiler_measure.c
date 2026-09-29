#include "boiler_measure.h"

#include <math.h>

#include "sp_util.h"

#define LOOP_ZERO_MA 4.0f
#define LOOP_SPAN_MA 16.0f

static float loop_fraction(float milliamps)
{
    return (milliamps - LOOP_ZERO_MA) / LOOP_SPAN_MA;
}

static BoilerReading scale(const BoilerConfig *config, float milliamps, float low, float high,
                           float steps_per_unit)
{
    BoilerReading reading = {0.0f, false};

    if (!(milliamps >= config->loop_fault_low_ma && milliamps <= config->loop_fault_high_ma)) {
        return reading;
    }
    reading.value = sp_quantize(low + loop_fraction(milliamps) * (high - low), steps_per_unit);
    reading.valid = true;
    return reading;
}

BoilerMeasurements boiler_measure(const BoilerConfig *config, const BoilerInputs *inputs)
{
    BoilerMeasurements measurements = {
        .temperature_c = scale(config, inputs->temperature_ma, config->temperature_min_c,
                               config->temperature_max_c, 10.0f),
        .pressure_bar = scale(config, inputs->pressure_ma, 0.0f, config->pressure_max_bar, 100.0f),
        .flow_lpm = scale(config, inputs->flow_ma, 0.0f, config->flow_max_lpm, 10.0f),
        .valve_position_pct = scale(config, inputs->valve_position_ma, 0.0f, 100.0f, 10.0f),
        .pump_running = inputs->pump_running != 0,
    };
    return measurements;
}

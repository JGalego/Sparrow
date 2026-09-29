#ifndef BOILER_FIXTURE_H
#define BOILER_FIXTURE_H

#include <string.h>

#include "boiler_controller.h"

#define STEP_MS 100

/*
 * Drives a controller with engineering-unit inputs. Sensor readings are
 * converted to loop currents with the default scaling, the way the plant does.
 */
typedef struct {
    BoilerController controller;
    BoilerInputs inputs;
    BoilerOutputs outputs;
    BoilerStatus status;
} BoilerFixture;

static inline float fixture_ma(float value, float low, float high)
{
    return 4.0f + 16.0f * (value - low) / (high - low);
}

static inline void fixture_set_temperature(BoilerFixture *f, float celsius)
{
    f->inputs.temperature_ma = fixture_ma(celsius, 0.0f, 150.0f);
}

static inline void fixture_set_pressure(BoilerFixture *f, float bar)
{
    f->inputs.pressure_ma = fixture_ma(bar, 0.0f, 6.0f);
}

static inline void fixture_set_flow(BoilerFixture *f, float lpm)
{
    f->inputs.flow_ma = fixture_ma(lpm, 0.0f, 100.0f);
}

static inline void fixture_set_valve_position(BoilerFixture *f, float percent)
{
    f->inputs.valve_position_ma = fixture_ma(percent, 0.0f, 100.0f);
}

/* Healthy, cold, idle plant: 20 degC, 1.2 bar, no flow, valve closed, pump stopped. */
static inline void fixture_init(BoilerFixture *f)
{
    const BoilerConfig config = boiler_config_default();

    memset(f, 0, sizeof *f);
    boiler_init(&f->controller, &config, NULL);
    fixture_set_temperature(f, 20.0f);
    fixture_set_pressure(f, 1.2f);
    fixture_set_flow(f, 0.0f);
    fixture_set_valve_position(f, 0.0f);
}

static inline void fixture_step_with(BoilerFixture *f, const BoilerCommands *commands)
{
    boiler_step(&f->controller, &f->inputs, commands, STEP_MS, &f->outputs);
    boiler_get_status(&f->controller, &f->status);
}

static inline void fixture_step(BoilerFixture *f)
{
    const BoilerCommands none = {0};

    fixture_step_with(f, &none);
}

static inline void fixture_run_ms(BoilerFixture *f, unsigned duration_ms)
{
    for (unsigned elapsed = 0; elapsed < duration_ms; elapsed += STEP_MS) {
        fixture_step(f);
    }
}

static inline void fixture_command(BoilerFixture *f, BoilerCommands commands)
{
    fixture_step_with(f, &commands);
}

/* Follows the actuator commands the way a healthy plant would: instant pump, instant valve. */
static inline void fixture_follow_outputs(BoilerFixture *f)
{
    f->inputs.pump_running = f->outputs.pump_run;
    fixture_set_valve_position(f, f->outputs.valve_open ? 100.0f : 0.0f);
    fixture_set_flow(f, f->outputs.pump_run && f->outputs.valve_open ? 30.0f : 0.0f);
}

static inline void fixture_step_following(BoilerFixture *f, unsigned duration_ms)
{
    for (unsigned elapsed = 0; elapsed < duration_ms; elapsed += STEP_MS) {
        fixture_step(f);
        fixture_follow_outputs(f);
    }
}

static inline void fixture_reach_standby(BoilerFixture *f)
{
    fixture_run_ms(f, f->controller.config.init_time_ms + STEP_MS);
}

/* Standby -> startup -> running with a healthy, cooperative plant. */
static inline void fixture_reach_running(BoilerFixture *f)
{
    const BoilerCommands start = {.start = 1};

    fixture_reach_standby(f);
    fixture_command(f, start);
    fixture_step_following(f, 8000);
}

#endif

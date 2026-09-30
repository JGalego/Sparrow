#ifndef GEAR_FIXTURE_H
#define GEAR_FIXTURE_H

#include <string.h>

#include "gear_controller.h"

#define STEP_MS 50

/*
 * Drives a controller with engineering-unit inputs. Hydraulic pressure is
 * converted to a loop current with the default scaling, the way the plant does.
 */
typedef struct {
    GearController controller;
    GearInputs inputs;
    GearOutputs outputs;
    GearStatus status;
} GearFixture;

typedef enum { FIXTURE_GEAR_UP, FIXTURE_GEAR_TRANSIT, FIXTURE_GEAR_DOWN } FixtureGearPosition;

static inline void fixture_set_hydraulic_psi(GearFixture *f, float psi)
{
    f->inputs.hydraulic_pressure_ma = 4.0f + 16.0f * psi / 5000.0f;
}

/* All three legs at the same position; TRANSIT means neither lock sensor is made. */
static inline void fixture_set_gear(GearFixture *f, FixtureGearPosition position)
{
    const uint8_t down = position == FIXTURE_GEAR_DOWN;
    const uint8_t up = position == FIXTURE_GEAR_UP;

    f->inputs.nose_downlock = f->inputs.left_downlock = f->inputs.right_downlock = down;
    f->inputs.nose_uplock = f->inputs.left_uplock = f->inputs.right_uplock = up;
}

static inline void fixture_set_ground(GearFixture *f, int on_ground)
{
    f->inputs.left_wow = f->inputs.right_wow = (uint8_t)on_ground;
}

/* Airborne at the given radio altitude and airspeed, both valid. */
static inline void fixture_fly(GearFixture *f, float altitude_ft, float airspeed_kt)
{
    fixture_set_ground(f, 0);
    f->inputs.radio_altitude_ft = altitude_ft;
    f->inputs.radio_altitude_valid = 1;
    f->inputs.airspeed_kt = airspeed_kt;
    f->inputs.airspeed_valid = 1;
}

/*
 * Parked on the runway: gear down and locked, lever down, squat switches
 * made, 3000 psi, 0 kt valid airspeed, radio altitude 0 ft. The controller
 * is freshly initialized with the default configuration and has not stepped.
 */
static inline void fixture_init(GearFixture *f)
{
    const GearConfig config = gear_config_default();

    memset(f, 0, sizeof *f);
    gear_init(&f->controller, &config, NULL);
    fixture_set_hydraulic_psi(f, 3000.0f);
    fixture_set_gear(f, FIXTURE_GEAR_DOWN);
    fixture_set_ground(f, 1);
    f->inputs.airspeed_valid = 1;
    f->inputs.radio_altitude_valid = 1;
    f->inputs.gear_handle_down = 1;
}

static inline void fixture_step_with(GearFixture *f, const GearCommands *commands)
{
    gear_step(&f->controller, &f->inputs, commands, STEP_MS, &f->outputs);
    gear_get_status(&f->controller, &f->status);
}

static inline void fixture_step(GearFixture *f)
{
    const GearCommands none = {0};

    fixture_step_with(f, &none);
}

/* Qualify arming through a valid airborne step; equality cannot establish the warning. */
static inline void fixture_arm_warning(GearFixture *f, const GearConfig *config)
{
    fixture_fly(f, config->gear_warning_altitude_ft, config->gear_warning_airspeed_kt);
    fixture_step(f);
}

static inline void fixture_run_ms(GearFixture *f, unsigned duration_ms)
{
    for (unsigned elapsed = 0; elapsed < duration_ms; elapsed += STEP_MS) {
        fixture_step(f);
    }
}

static inline void fixture_command(GearFixture *f, GearCommands commands)
{
    fixture_step_with(f, &commands);
}

/* The alarm bit is active now or latched from an earlier activation. */
static inline int fixture_raised(const GearFixture *f, unsigned fault)
{
    return ((f->status.alarms_active | f->status.alarms_latched) & (UINT32_C(1) << fault)) != 0;
}

#endif

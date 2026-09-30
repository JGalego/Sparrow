#include "gear_fixture.h"
#include "sp_test.h"

static void alternate_step(GearFixture *f, uint32_t dt_ms)
{
    gear_step(&f->controller, &f->inputs, NULL, dt_ms, &f->outputs);
    gear_get_status(&f->controller, &f->status);
}

SP_TEST(alternate_cancels_timeout_and_release_gets_full_fresh_interval, "REQ-007,REQ-009")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint8_t alternate_values[] = {1, 2, UINT8_MAX};
    const uint32_t elapsed_times[] = {config.normal_transit_timeout_ms - 1U,
                                      config.normal_transit_timeout_ms,
                                      config.normal_transit_timeout_ms + 1U};

    SP_ASSERT(config.normal_transit_timeout_ms > 1U);
    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    for (unsigned started = 0; started < 2; started++) {
        for (unsigned handle_down = 0; handle_down < 2; handle_down++) {
            for (size_t boundary = 0; boundary < sizeof elapsed_times / sizeof elapsed_times[0];
                 boundary++) {
                GearFixture f;

                fixture_init(&f);
                fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
                f.inputs.gear_handle_down = (uint8_t)handle_down;
                if (started != 0) {
                    alternate_step(&f, 1);
                    SP_ASSERT_EQ_INT(handle_down, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(handle_down == 0, f.outputs.up_valve);
                    alternate_step(&f, config.normal_transit_timeout_ms - 1U);
                    SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
                }

                /* Pulling takes priority even when this interval would expire the timer. */
                f.inputs.alternate_extend = alternate_values[boundary];
                fixture_set_hydraulic_psi(&f, 0.0f);
                alternate_step(&f, elapsed_times[boundary]);
                SP_ASSERT_EQ_INT(GEAR_STATE_ALTERNATE_EXTENDING, f.status.state);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(0, f.outputs.master_warning);

                /* An incomplete free-fall transit cannot accumulate a hydraulic timeout. */
                alternate_step(&f, config.normal_transit_timeout_ms + 1U);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);

                /* Keep the same lever direction: reversal must not mask a stale timer. */
                f.inputs.alternate_extend = 0;
                alternate_step(&f, config.normal_transit_timeout_ms + 1U);
                SP_ASSERT_EQ_INT(handle_down, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(handle_down == 0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(0, f.outputs.master_warning);

                /* The new hydraulic transit must not fault just below its full limit. */
                alternate_step(&f, config.normal_transit_timeout_ms - 1U);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(0, f.outputs.master_warning);

                /* At and above the fresh limit, an incomplete transit must fault. */
                for (unsigned expired_step = 0; expired_step < 2; expired_step++) {
                    alternate_step(&f, 1);
                    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
                    SP_ASSERT_EQ_INT(handle_down, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(handle_down == 0, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
                }
            }
        }
    }
}

SP_TEST(alternate_release_does_not_start_timeout_before_retraction_drive, "REQ-003,REQ-007,REQ-009")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    GearFixture f;

    SP_ASSERT(config.normal_transit_timeout_ms > 1U);
    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    fixture_init(&f);
    fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
    fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
    f.inputs.gear_handle_down = 0;
    alternate_step(&f, 1);
    SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
    alternate_step(&f, config.normal_transit_timeout_ms - 1U);
    SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);

    f.inputs.alternate_extend = 1;
    alternate_step(&f, 1);
    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
    SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
    SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);

    f.inputs.alternate_extend = 0;
    for (unsigned inhibition = 0; inhibition < 3; inhibition++) {
        f.inputs.airspeed_kt = config.min_retraction_airspeed_kt;
        f.inputs.airspeed_valid = 1;
        fixture_set_ground(&f, 0);
        if (inhibition == 0) {
            f.inputs.airspeed_kt = config.min_retraction_airspeed_kt - 1.0f;
        } else if (inhibition == 1) {
            f.inputs.airspeed_valid = 0;
        } else {
            fixture_set_ground(&f, 1);
        }
        alternate_step(&f, config.normal_transit_timeout_ms + 1U);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
        SP_ASSERT_EQ_INT(inhibition == 2, f.outputs.handle_lock);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
    }

    /* Permission at the speed boundary starts drive, not a retroactive timeout. */
    fixture_set_ground(&f, 0);
    alternate_step(&f, config.normal_transit_timeout_ms + 1U);
    SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
    SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
    alternate_step(&f, config.normal_transit_timeout_ms - 1U);
    SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
    SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
    SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
    for (unsigned expired_step = 0; expired_step < 2; expired_step++) {
        alternate_step(&f, 1);
        SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & fault) != 0);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
    }
}

SP_TEST(alternate_preserves_release_and_fault_latch_after_lock_completion, "REQ-008,REQ-009")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint8_t alternate_values[] = {1, 2, UINT8_MAX};

    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    for (unsigned handle_down = 0; handle_down < 2; handle_down++) {
        for (unsigned completed_down = 0; completed_down < 2; completed_down++) {
            GearFixture f;

            fixture_init(&f);
            fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
            fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
            f.inputs.gear_handle_down = handle_down != 0 ? 2 : 0;
            alternate_step(&f, 1);
            alternate_step(&f, config.normal_transit_timeout_ms);
            SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

            /* Exercise both completed positions, in agreement and disagreement. */
            fixture_set_gear(&f, completed_down != 0 ? FIXTURE_GEAR_DOWN : FIXTURE_GEAR_UP);
            f.inputs.airspeed_valid = 0;
            f.inputs.airspeed_kt = config.min_retraction_airspeed_kt - 1.0f;
            for (size_t held = 0; held < sizeof alternate_values / sizeof alternate_values[0];
                 held++) {
                f.inputs.alternate_extend = alternate_values[held];
                f.inputs.hydraulic_pressure_ma = held == 0 ? NAN : 21.0f;
                f.inputs.left_wow = held == 0 ? 2 : 0;
                f.inputs.right_wow = held == 1 ? UINT8_MAX : 0;
                alternate_step(&f, config.normal_transit_timeout_ms + 1U);

                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
                SP_ASSERT_EQ_INT(held < 2, f.outputs.handle_lock);
                SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
            }

            /* Agreement and release alone cannot reset the pre-existing latch. */
            f.inputs.alternate_extend = 0;
            alternate_step(&f, 1);
            SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
            SP_ASSERT_EQ_INT(handle_down != 0 && completed_down == 0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
            SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
        }
    }
}

SP_TEST(alternate_held_survives_live_lever_and_every_lock_combination, "REQ-009")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const float speeds[] = {nextafterf(config.min_retraction_airspeed_kt, -INFINITY),
                            config.min_retraction_airspeed_kt,
                            nextafterf(config.min_retraction_airspeed_kt, INFINITY), NAN};
    const float pressures[] = {nextafterf(4.0f, -INFINITY),
                               4.0f,
                               nextafterf(4.0f, INFINITY),
                               nextafterf(20.0f, -INFINITY),
                               20.0f,
                               nextafterf(20.0f, INFINITY),
                               NAN};

    for (unsigned faulted = 0; faulted < 2; faulted++) {
        GearFixture f;

        fixture_init(&f);
        fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
        fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
        f.inputs.gear_handle_down = 0;
        alternate_step(&f, 1);
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
        if (faulted != 0) {
            alternate_step(&f, config.normal_transit_timeout_ms);
        }
        SP_ASSERT_EQ_INT(faulted, (f.status.alarms_active & fault) != 0);
        SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);

        /* Pull once; subsequent steps must not depend on another pull edge. */
        f.inputs.alternate_extend = UINT8_MAX;
        alternate_step(&f, 1);
        SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);

        /* Includes partial, complete and contradictory indications for both directions. */
        for (unsigned mask = 0; mask < 64; mask++) {
            f.inputs.nose_downlock = (mask & 0x01U) != 0 ? 2 : 0;
            f.inputs.left_downlock = (mask & 0x02U) != 0 ? 2 : 0;
            f.inputs.right_downlock = (mask & 0x04U) != 0 ? 2 : 0;
            f.inputs.nose_uplock = (mask & 0x08U) != 0 ? 2 : 0;
            f.inputs.left_uplock = (mask & 0x10U) != 0 ? 2 : 0;
            f.inputs.right_uplock = (mask & 0x20U) != 0 ? 2 : 0;
            f.inputs.left_wow = (mask & 0x01U) != 0 ? 2 : 0;
            f.inputs.right_wow = (mask & 0x02U) != 0 ? UINT8_MAX : 0;
            f.inputs.airspeed_valid = (mask & 0x04U) != 0 ? 0 : 2;
            f.inputs.airspeed_kt = speeds[mask % (sizeof speeds / sizeof speeds[0])];
            f.inputs.hydraulic_pressure_ma =
                pressures[mask % (sizeof pressures / sizeof pressures[0])];

            /* Reverse the lever under each indication without releasing alternate. */
            for (unsigned handle_down = 0; handle_down < 2; handle_down++) {
                f.inputs.gear_handle_down = handle_down != 0 ? 2 : 0;
                alternate_step(&f, STEP_MS);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
                SP_ASSERT_EQ_INT((mask & 0x03U) != 0, f.outputs.handle_lock);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_unacked & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, f.outputs.master_warning);
                SP_ASSERT_EQ_INT(faulted != 0 ? GEAR_STATE_FAULT : GEAR_STATE_ALTERNATE_EXTENDING,
                                 f.status.state);
            }
        }
    }
}

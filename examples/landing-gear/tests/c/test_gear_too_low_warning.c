#include "gear_fixture.h"
#include "sp_test.h"

SP_TEST(warning_thresholds_are_strict_and_clear_on_crossing, "REQ-010")
{
    const GearConfig defaults = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const int offsets[] = {-1, 0, 1, -1};
    const size_t count = sizeof offsets / sizeof offsets[0];

    SP_ASSERT_NEAR(500.0f, defaults.gear_warning_altitude_ft, 0.0f);
    SP_ASSERT_NEAR(190.0f, defaults.gear_warning_airspeed_kt, 0.0f);

    for (unsigned configured = 0; configured < 2; configured++) {
        GearConfig config = defaults;
        GearFixture f;

        /* Changed limits detect comparisons against hard-coded default values. */
        config.gear_warning_altitude_ft += (float)configured;
        config.gear_warning_airspeed_kt += (float)configured;
        fixture_init(&f);
        SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
        fixture_set_gear(&f, FIXTURE_GEAR_UP);
        f.inputs.gear_handle_down = 0;
        fixture_arm_warning(&f, &config);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);

        /* Cover both limits independently and together, then return below them.
         * Reuse the controller so equality and above-limit readings must also
         * remove a warning that was already active.
         */
        for (size_t altitude = 0; altitude < count; altitude++) {
            for (size_t speed = 0; speed < count; speed++) {
                const int expected = offsets[altitude] < 0 && offsets[speed] < 0;

                fixture_fly(&f, config.gear_warning_altitude_ft + (float)offsets[altitude],
                            config.gear_warning_airspeed_kt + (float)offsets[speed]);
                fixture_step(&f);

                SP_ASSERT_EQ_INT(expected, (f.status.alarms_active & warning) != 0);
            }
        }
    }
}

SP_TEST(warning_requires_both_validity_inputs_and_recovers, "REQ-010")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const uint8_t validity_values[] = {0, 1, 2, UINT8_MAX};
    const size_t count = sizeof validity_values / sizeof validity_values[0];

    for (size_t altitude = 0; altitude < count; altitude++) {
        for (size_t speed = 0; speed < count; speed++) {
            GearFixture f;
            const int expected = validity_values[altitude] != 0 && validity_values[speed] != 0;

            fixture_init(&f);
            fixture_set_gear(&f, FIXTURE_GEAR_UP);
            f.inputs.gear_handle_down = 0;
            fixture_arm_warning(&f, &config);
            SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
            fixture_fly(&f, config.gear_warning_altitude_ft - 1.0f,
                        config.gear_warning_airspeed_kt - 1.0f);
            f.inputs.radio_altitude_valid = validity_values[altitude];
            f.inputs.airspeed_valid = validity_values[speed];
            fixture_step(&f);
            SP_ASSERT_EQ_INT(expected, (f.status.alarms_active & warning) != 0);

            /* Validity recovery must allow the condition to establish. */
            f.inputs.radio_altitude_valid = 1;
            f.inputs.airspeed_valid = 1;
            fixture_step(&f);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);

            /* A failed sensor reporting zero must not preserve an existing
             * warning merely because its numeric reading is below the limit.
             */
            f.inputs.radio_altitude_ft = 0.0f;
            f.inputs.airspeed_kt = 0.0f;
            f.inputs.radio_altitude_valid = validity_values[altitude];
            f.inputs.airspeed_valid = validity_values[speed];
            fixture_step(&f);
            SP_ASSERT_EQ_INT(expected, (f.status.alarms_active & warning) != 0);

            f.inputs.radio_altitude_valid = 1;
            f.inputs.airspeed_valid = 1;
            fixture_step(&f);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        }
    }
}

SP_TEST(warning_is_independent_of_operation_and_disagree_latch, "REQ-010")
{
    const GearConfig config = gear_config_default();
    const GearCommands none = {0};
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const uint32_t disagree = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint8_t discrete_values[] = {0, 1, 2, UINT8_MAX};
    const size_t count = sizeof discrete_values / sizeof discrete_values[0];

    for (unsigned latched = 0; latched < 2; latched++) {
        for (size_t handle = 0; handle < count; handle++) {
            for (size_t alternate = 0; alternate < count; alternate++) {
                GearFixture f;

                fixture_init(&f);
                fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
                fixture_fly(&f, config.gear_warning_altitude_ft + 1.0f,
                            config.gear_warning_airspeed_kt - 1.0f);
                fixture_step(&f);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);

                if (latched != 0) {
                    /* Establish a real fault through the public transit API,
                     * rather than modifying controller internals.
                     */
                    gear_step(&f.controller, &f.inputs, &none, config.normal_transit_timeout_ms,
                              &f.outputs);
                    gear_get_status(&f.controller, &f.status);
                    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
                }
                SP_ASSERT_EQ_INT(latched, (f.status.alarms_latched & disagree) != 0);

                f.inputs.gear_handle_down = discrete_values[handle];
                f.inputs.alternate_extend = discrete_values[alternate];
                for (unsigned phase = 0; phase < 7; phase++) {
                    fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
                    fixture_fly(&f, config.gear_warning_altitude_ft - 1.0f,
                                config.gear_warning_airspeed_kt - 1.0f);
                    if (phase == 1) {
                        f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft + 1.0f;
                    } else if (phase == 2) {
                        f.inputs.airspeed_kt = config.gear_warning_airspeed_kt + 1.0f;
                    } else if (phase == 3) {
                        f.inputs.radio_altitude_valid = 0;
                    } else if (phase == 4) {
                        f.inputs.airspeed_valid = 0;
                    } else if (phase == 5) {
                        fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
                        f.inputs.nose_downlock = 2;
                        f.inputs.left_downlock = UINT8_MAX;
                        f.inputs.right_downlock = 2;
                    }
                    fixture_step(&f);

                    /* Neither operating mode nor the fault may suppress a
                     * present condition or invent one with a missing predicate.
                     * The final phase re-establishes the warning after completion.
                     */
                    SP_ASSERT_EQ_INT(phase == 0 || phase == 6,
                                     (f.status.alarms_active & warning) != 0);
                    SP_ASSERT_EQ_INT(latched, (f.status.alarms_latched & disagree) != 0);
                }
            }
        }
    }
}

SP_TEST(warning_requires_arming_and_uses_same_step_ground_disarming, "REQ-010,REQ-014")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);

    for (unsigned wow = 1; wow < 4; wow++) {
        GearFixture f;

        fixture_init(&f);
        fixture_set_gear(&f, FIXTURE_GEAR_UP);
        f.inputs.gear_handle_down = 0;
        fixture_fly(&f, config.gear_warning_altitude_ft - 1.0f,
                    config.gear_warning_airspeed_kt - 1.0f);
        fixture_step(&f);

        /* Every other predicate is true, but an initially disarmed warning is absent. */
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        fixture_arm_warning(&f, &config);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        fixture_fly(&f, config.gear_warning_altitude_ft - 1.0f,
                    config.gear_warning_airspeed_kt - 1.0f);
        fixture_step(&f);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);

        /* Either nonzero WOW input must remove an active condition in this very step. */
        f.inputs.left_wow = (wow & 1U) != 0 ? 2 : 0;
        f.inputs.right_wow = (wow & 2U) != 0 ? UINT8_MAX : 0;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);

        /* Becoming airborne below the limit cannot resurrect the previous arming. */
        fixture_set_ground(&f, 0);
        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);

        fixture_arm_warning(&f, &config);
        fixture_fly(&f, config.gear_warning_altitude_ft - 1.0f,
                    config.gear_warning_airspeed_kt - 1.0f);
        fixture_step(&f);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);

        /* Reinitialization also removes arming despite unchanged valid low readings. */
        SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
    }
}

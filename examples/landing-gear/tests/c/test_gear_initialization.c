#include "gear_fixture.h"
#include "sp_test.h"

enum { DOWN_LOCK_MASK = 0x07, UP_LOCK_MASK = 0x38, LOCK_COMBINATIONS = 64 };

/* Bits 0..2 are nose/left/right downlocks; bits 3..5 are the uplocks. */
static void set_lock_mask(GearInputs *inputs, unsigned mask, uint8_t asserted)
{
    inputs->nose_downlock = (mask & 0x01U) != 0 ? asserted : 0;
    inputs->left_downlock = (mask & 0x02U) != 0 ? asserted : 0;
    inputs->right_downlock = (mask & 0x04U) != 0 ? asserted : 0;
    inputs->nose_uplock = (mask & 0x08U) != 0 ? asserted : 0;
    inputs->left_uplock = (mask & 0x10U) != 0 ? asserted : 0;
    inputs->right_uplock = (mask & 0x20U) != 0 ? asserted : 0;
}

SP_TEST(init_starts_in_init_with_actuators_off, "REQ-001")
{
    const GearConfig config = gear_config_default();
    const GearInputs inputs = {0};
    const GearCommands commands = {0};
    GearController controller;
    GearOutputs outputs;
    GearStatus status;

    /* Initialization must erase stale actuator commands and state. */
    memset(&controller, 0xa5, sizeof controller);
    memset(&outputs, 0xa5, sizeof outputs);
    SP_ASSERT_EQ_INT(SP_OK, gear_init(&controller, &config, NULL));
    gear_get_status(&controller, &status);
    SP_ASSERT_EQ_INT(GEAR_STATE_INIT, status.state);

    /* A zero-duration call exposes initialized outputs without taking a control step. */
    gear_step(&controller, &inputs, &commands, 0, &outputs);
    gear_get_status(&controller, &status);
    SP_ASSERT_EQ_INT(GEAR_STATE_INIT, status.state);
    SP_ASSERT_EQ_INT(0, outputs.down_valve);
    SP_ASSERT_EQ_INT(0, outputs.up_valve);
    SP_ASSERT_EQ_INT(0, outputs.uplock_release);
}

SP_TEST(first_step_classifies_every_lock_combination, "REQ-001")
{
    const GearConfig config = gear_config_default();
    const uint8_t asserted_values[] = {1, 2, UINT8_MAX};

    /* Includes two versus three locks and each possible opposite-lock assertion. */
    for (size_t value = 0; value < sizeof asserted_values / sizeof asserted_values[0]; value++) {
        for (unsigned mask = 0; mask < LOCK_COMBINATIONS; mask++) {
            for (unsigned handle_down = 0; handle_down < 2; handle_down++) {
                GearFixture f;
                const int complete =
                    handle_down != 0 ? mask == DOWN_LOCK_MASK : mask == UP_LOCK_MASK;
                const GearState expected =
                    handle_down != 0 ? (complete ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_EXTENDING)
                                     : (complete ? GEAR_STATE_UP_LOCKED : GEAR_STATE_RETRACTING);

                fixture_init(&f);
                fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                set_lock_mask(&f.inputs, mask, asserted_values[value]);
                f.inputs.gear_handle_down = handle_down != 0 ? asserted_values[value] : 0;
                fixture_step(&f);

                SP_ASSERT_EQ_INT(expected, f.status.state);
                SP_ASSERT_EQ_INT(handle_down != 0 && !complete, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(handle_down == 0 && !complete, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
                SP_ASSERT_EQ_INT(0, f.status.alarms_active);
                SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
            }
        }
    }
}

SP_TEST(first_step_retraction_obeys_airspeed_boundary, "REQ-001,REQ-003")
{
    const GearConfig config = gear_config_default();
    const unsigned masks[] = {0, DOWN_LOCK_MASK, UP_LOCK_MASK};

    for (int offset = -1; offset <= 1; offset++) {
        for (size_t position = 0; position < sizeof masks / sizeof masks[0]; position++) {
            GearFixture f;
            const int complete = masks[position] == UP_LOCK_MASK;

            fixture_init(&f);
            fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt + (float)offset);
            set_lock_mask(&f.inputs, masks[position], 1);
            f.inputs.gear_handle_down = 0;
            fixture_step(&f);

            SP_ASSERT_EQ_INT(complete ? GEAR_STATE_UP_LOCKED : GEAR_STATE_RETRACTING,
                             f.status.state);
            SP_ASSERT_EQ_INT(!complete && offset >= 0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
        }
    }
}

SP_TEST(first_step_retraction_requires_airborne_and_valid_speed, "REQ-001,REQ-003")
{
    const GearConfig config = gear_config_default();
    const unsigned masks[] = {0, DOWN_LOCK_MASK};
    const uint8_t validity_values[] = {0, 1, 2, UINT8_MAX};

    for (size_t position = 0; position < sizeof masks / sizeof masks[0]; position++) {
        for (unsigned wow = 0; wow < 4; wow++) {
            for (size_t valid = 0; valid < sizeof validity_values / sizeof validity_values[0];
                 valid++) {
                GearFixture f;

                fixture_init(&f);
                fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                set_lock_mask(&f.inputs, masks[position], 1);
                f.inputs.gear_handle_down = 0;
                f.inputs.left_wow = (wow & 1U) != 0 ? UINT8_MAX : 0;
                f.inputs.right_wow = (wow & 2U) != 0 ? 2 : 0;
                f.inputs.airspeed_valid = validity_values[valid];
                fixture_step(&f);

                SP_ASSERT_EQ_INT(GEAR_STATE_RETRACTING, f.status.state);
                SP_ASSERT_EQ_INT(wow == 0 && validity_values[valid] != 0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
            }
        }
    }
}

SP_TEST(first_step_extension_is_not_blocked_by_retraction_interlock, "REQ-001,REQ-004")
{
    const GearConfig config = gear_config_default();
    const unsigned masks[] = {0, DOWN_LOCK_MASK, UP_LOCK_MASK, 0x3f};

    for (size_t position = 0; position < sizeof masks / sizeof masks[0]; position++) {
        for (unsigned wow = 0; wow < 4; wow++) {
            for (unsigned valid = 0; valid < 2; valid++) {
                GearFixture f;
                const int complete = masks[position] == DOWN_LOCK_MASK;

                fixture_init(&f);
                fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt - 1.0f);
                set_lock_mask(&f.inputs, masks[position], 1);
                f.inputs.left_wow = (wow & 1U) != 0;
                f.inputs.right_wow = (wow & 2U) != 0;
                f.inputs.airspeed_valid = (uint8_t)valid;
                fixture_step(&f);

                SP_ASSERT_EQ_INT(complete ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_EXTENDING,
                                 f.status.state);
                SP_ASSERT_EQ_INT(!complete, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
            }
        }
    }
}

SP_TEST(first_step_alternate_extension_has_priority, "REQ-001,REQ-009")
{
    const GearConfig config = gear_config_default();
    const unsigned masks[] = {0, DOWN_LOCK_MASK, UP_LOCK_MASK, 0x3f};
    const uint8_t alternate_values[] = {1, 2, UINT8_MAX};

    for (size_t position = 0; position < sizeof masks / sizeof masks[0]; position++) {
        for (unsigned handle_down = 0; handle_down < 2; handle_down++) {
            for (unsigned permission_case = 0; permission_case < 6; permission_case++) {
                for (size_t alternate = 0;
                     alternate < sizeof alternate_values / sizeof alternate_values[0];
                     alternate++) {
                    GearFixture f;

                    fixture_init(&f);
                    fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                    set_lock_mask(&f.inputs, masks[position], 1);
                    f.inputs.gear_handle_down = (uint8_t)handle_down;
                    f.inputs.alternate_extend = alternate_values[alternate];
                    if (permission_case == 1) {
                        f.inputs.airspeed_kt = config.min_retraction_airspeed_kt - 1.0f;
                    } else if (permission_case == 2) {
                        f.inputs.airspeed_valid = 0;
                    } else if (permission_case >= 3) {
                        const unsigned wow = permission_case - 2;

                        f.inputs.left_wow = (wow & 1U) != 0;
                        f.inputs.right_wow = (wow & 2U) != 0;
                    }
                    fixture_step(&f);

                    SP_ASSERT_EQ_INT(GEAR_STATE_ALTERNATE_EXTENDING, f.status.state);
                    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
                    SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
                }
            }
        }
    }
}

SP_TEST(startup_contradictory_locks_cannot_suppress_warning, "REQ-001,REQ-010,REQ-014")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);

    for (unsigned mask = 0; mask < LOCK_COMBINATIONS; mask++) {
        GearFixture f;
        const int down_locked = mask == DOWN_LOCK_MASK;

        fixture_init(&f);
        fixture_fly(&f, config.gear_warning_altitude_ft - 1.0f,
                    config.gear_warning_airspeed_kt - 1.0f);
        set_lock_mask(&f.inputs, mask, 1);
        fixture_step(&f);

        /* Startup classifies the locks, but low altitude alone cannot arm the warning. */
        SP_ASSERT_EQ_INT(down_locked ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_EXTENDING,
                         f.status.state);
        SP_ASSERT_EQ_INT(!down_locked, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.status.alarms_latched);

        fixture_arm_warning(&f, &config);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        fixture_fly(&f, config.gear_warning_altitude_ft - 1.0f,
                    config.gear_warning_airspeed_kt - 1.0f);
        fixture_step(&f);

        /* Once armed, only exclusive three-downlock agreement suppresses the warning. */
        SP_ASSERT_EQ_INT(!down_locked, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
    }
}

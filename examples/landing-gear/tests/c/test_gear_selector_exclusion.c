#include "gear_fixture.h"
#include "sp_test.h"

/* Observe only commands and status published through the public interfaces. */
static int selector_step_exclusive(GearFixture *f, const GearCommands *commands, uint32_t dt_ms)
{
    gear_step(&f->controller, &f->inputs, commands, dt_ms, &f->outputs);
    gear_get_status(&f->controller, &f->status);
    return f->outputs.down_valve == 0 || f->outputs.up_valve == 0;
}

SP_TEST(selector_initialization_clears_commands_from_every_drive_mode, "REQ-006")
{
    const GearConfig config = gear_config_default();
    GearFixture f;

    fixture_init(&f);
    /* Initialization must not depend on zero-filled controller storage. */
    memset(&f.controller, 0xa5, sizeof f.controller);
    SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
    SP_ASSERT(selector_step_exclusive(&f, NULL, 0));
    SP_ASSERT_EQ_INT(GEAR_STATE_INIT, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
    SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);

    fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
    fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
    for (unsigned operation = 0; operation < 3; operation++) {
        f.inputs.gear_handle_down = operation == 0 ? 1 : 0;
        f.inputs.alternate_extend = operation == 2 ? 2 : 0;
        SP_ASSERT(selector_step_exclusive(&f, NULL, 1));
        SP_ASSERT_EQ_INT(operation == 0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(operation == 1, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(operation == 2, f.outputs.uplock_release);

        /* Reinitialize with an extension, retraction, or free-fall command outstanding. */
        SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
        SP_ASSERT(selector_step_exclusive(&f, NULL, 0));
        SP_ASSERT_EQ_INT(GEAR_STATE_INIT, f.status.state);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
        SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
        SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
    }
}

SP_TEST(selector_alternate_handoffs_respect_exclusion_and_permission, "REQ-006,REQ-009")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint8_t asserted_values[] = {1, 2, UINT8_MAX};
    const float speeds[] = {config.min_retraction_airspeed_kt - 1.0f,
                            config.min_retraction_airspeed_kt,
                            config.min_retraction_airspeed_kt + 1.0f};

    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    for (size_t value = 0; value < sizeof asserted_values / sizeof asserted_values[0]; value++) {
        const uint8_t asserted = asserted_values[value];

        for (unsigned source_down = 0; source_down < 2; source_down++) {
            for (unsigned target_down = 0; target_down < 2; target_down++) {
                for (size_t speed = 0; speed < sizeof speeds / sizeof speeds[0]; speed++) {
                    /* Permission, left WOW, right WOW, both WOW, or invalid airspeed. */
                    for (unsigned inhibition = 0; inhibition < 5; inhibition++) {
                        GearFixture f;
                        const int permitted = speed >= 1 && inhibition == 0;

                        fixture_init(&f);
                        fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                        fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
                        f.inputs.gear_handle_down = source_down != 0 ? asserted : 0;
                        SP_ASSERT(selector_step_exclusive(&f, NULL, 1));
                        SP_ASSERT_EQ_INT(source_down, f.outputs.down_valve);
                        SP_ASSERT_EQ_INT(source_down == 0, f.outputs.up_valve);

                        /* Alternate extension must replace either outstanding selector command. */
                        f.inputs.alternate_extend = asserted;
                        f.inputs.gear_handle_down = target_down != 0 ? asserted : 0;
                        f.inputs.airspeed_kt = speeds[speed];
                        f.inputs.airspeed_valid = inhibition == 4 ? 0 : asserted;
                        f.inputs.left_wow = inhibition == 1 || inhibition == 3 ? asserted : 0;
                        f.inputs.right_wow = inhibition == 2 || inhibition == 3 ? asserted : 0;
                        fixture_set_hydraulic_psi(&f, 0.0f);
                        SP_ASSERT(selector_step_exclusive(&f, NULL,
                                                          config.normal_transit_timeout_ms + 1U));
                        SP_ASSERT_EQ_INT(GEAR_STATE_ALTERNATE_EXTENDING, f.status.state);
                        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                        SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
                        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                        SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
                        SP_ASSERT_EQ_INT(0, f.outputs.master_warning);

                        /* Holding alternate also ignores contradictory lock indications. */
                        f.inputs.nose_downlock = asserted;
                        f.inputs.nose_uplock = asserted;
                        SP_ASSERT(selector_step_exclusive(&f, NULL,
                                                          config.normal_transit_timeout_ms + 1U));
                        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                        SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
                        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                        SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);

                        fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
                        f.inputs.alternate_extend = 0;
                        /* A zero-duration call publishes the previous safe command unchanged. */
                        SP_ASSERT(selector_step_exclusive(&f, NULL, 0));
                        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                        SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);

                        /* Release resumes the new direction, not the pre-alternate command. */
                        SP_ASSERT(selector_step_exclusive(&f, NULL, 1));
                        SP_ASSERT_EQ_INT(target_down, f.outputs.down_valve);
                        SP_ASSERT_EQ_INT(target_down == 0 && permitted, f.outputs.up_valve);
                        SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
                        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                        SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
                        SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
                    }
                }
            }
        }
    }
}

SP_TEST(selector_fault_entry_reversal_alternate_and_reset_remain_exclusive,
        "REQ-006,REQ-007,REQ-008,REQ-009")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const GearCommands reset_and_mute = {.mute = UINT8_MAX, .reset = UINT8_MAX};
    const uint32_t elapsed_times[] = {config.normal_transit_timeout_ms - 1U,
                                      config.normal_transit_timeout_ms,
                                      config.normal_transit_timeout_ms + 1U};

    SP_ASSERT(config.normal_transit_timeout_ms > 1U);
    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    for (unsigned initial_down = 0; initial_down < 2; initial_down++) {
        for (size_t boundary = 0; boundary < sizeof elapsed_times / sizeof elapsed_times[0];
             boundary++) {
            GearFixture f;
            const int expired = boundary >= 1;
            const int reverse_down = initial_down == 0;

            fixture_init(&f);
            fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
            fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
            f.inputs.gear_handle_down = (uint8_t)initial_down;
            SP_ASSERT(selector_step_exclusive(&f, NULL, 1));
            SP_ASSERT_EQ_INT(initial_down, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(initial_down == 0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);

            /* Fault entry must not introduce a second selector command. */
            SP_ASSERT(selector_step_exclusive(&f, NULL, elapsed_times[boundary]));
            SP_ASSERT_EQ_INT(initial_down, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(initial_down == 0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(expired, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(expired, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(expired, f.outputs.master_warning);
            SP_ASSERT_EQ_INT(
                expired ? GEAR_STATE_FAULT
                        : (initial_down != 0 ? GEAR_STATE_EXTENDING : GEAR_STATE_RETRACTING),
                f.status.state);
            if (!expired) {
                SP_ASSERT(selector_step_exclusive(&f, NULL, 1));
            }
            SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

            /* Reverse while faulted; an unresolved reset and mute cannot clear the latch. */
            f.inputs.gear_handle_down = reverse_down ? UINT8_MAX : 0;
            SP_ASSERT(selector_step_exclusive(&f, &reset_and_mute, 1));
            SP_ASSERT_EQ_INT(reverse_down, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(!reverse_down, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

            /* Fault handling must still inhibit retraction without selecting extension. */
            f.inputs.left_wow = 2;
            f.inputs.right_wow = UINT8_MAX;
            f.inputs.airspeed_valid = 0;
            SP_ASSERT(selector_step_exclusive(&f, NULL, 1));
            SP_ASSERT_EQ_INT(reverse_down, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);

            f.inputs.alternate_extend = UINT8_MAX;
            fixture_set_hydraulic_psi(&f, 0.0f);
            SP_ASSERT(selector_step_exclusive(&f, &reset_and_mute,
                                              config.normal_transit_timeout_ms + 1U));
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
            SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
            SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

            f.inputs.alternate_extend = 0;
            fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt + 1.0f);
            SP_ASSERT(selector_step_exclusive(&f, NULL, 1));
            SP_ASSERT_EQ_INT(reverse_down, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(!reverse_down, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
            SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
            SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

            /* Completion removes drive, but only an accepted reset removes the fault. */
            fixture_set_gear(&f, reverse_down ? FIXTURE_GEAR_DOWN : FIXTURE_GEAR_UP);
            SP_ASSERT(selector_step_exclusive(&f, NULL, 1));
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

            SP_ASSERT(selector_step_exclusive(&f, &reset_and_mute, 1));
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
            SP_ASSERT_EQ_INT(reverse_down ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_UP_LOCKED,
                             f.status.state);
        }
    }
}

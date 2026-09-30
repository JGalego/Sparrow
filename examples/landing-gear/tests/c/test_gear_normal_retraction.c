#include "gear_fixture.h"
#include "sp_test.h"

enum { RETRACTION_UP_LOCK_MASK = 0x38, RETRACTION_LOCK_COMBINATIONS = 64 };

/* Bits 0..2 are nose/left/right downlocks; bits 3..5 are the uplocks. */
static void set_retraction_lock_mask(GearInputs *inputs, unsigned mask, uint8_t asserted)
{
    inputs->nose_downlock = (mask & 0x01U) != 0 ? asserted : 0;
    inputs->left_downlock = (mask & 0x02U) != 0 ? asserted : 0;
    inputs->right_downlock = (mask & 0x04U) != 0 ? asserted : 0;
    inputs->nose_uplock = (mask & 0x08U) != 0 ? asserted : 0;
    inputs->left_uplock = (mask & 0x10U) != 0 ? asserted : 0;
    inputs->right_uplock = (mask & 0x20U) != 0 ? asserted : 0;
}

static void retraction_step_ms(GearFixture *f, uint32_t dt_ms)
{
    const GearCommands commands = {0};

    gear_step(&f->controller, &f->inputs, &commands, dt_ms, &f->outputs);
    gear_get_status(&f->controller, &f->status);
}

SP_TEST(completed_retraction_rechecks_every_lock_combination, "REQ-005,REQ-001,REQ-006")
{
    const GearConfig config = gear_config_default();
    const uint8_t asserted_values[] = {1, 2, UINT8_MAX};

    for (size_t value = 0; value < sizeof asserted_values / sizeof asserted_values[0]; value++) {
        GearFixture f;
        const uint8_t asserted = asserted_values[value];

        fixture_init(&f);
        fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
        fixture_step(&f);
        SP_ASSERT_EQ_INT(GEAR_STATE_DOWN_LOCKED, f.status.state);

        f.inputs.gear_handle_down = 0;
        retraction_step_ms(&f, 1);
        SP_ASSERT_EQ_INT(GEAR_STATE_RETRACTING, f.status.state);
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);

        set_retraction_lock_mask(&f.inputs, RETRACTION_UP_LOCK_MASK, asserted);
        retraction_step_ms(&f, 1);
        SP_ASSERT_EQ_INT(GEAR_STATE_UP_LOCKED, f.status.state);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);

        for (unsigned mask = 0; mask < RETRACTION_LOCK_COMBINATIONS; mask++) {
            const int complete = mask == RETRACTION_UP_LOCK_MASK;

            /* Includes two uplocks, exactly three, and three plus a contrary downlock. */
            set_retraction_lock_mask(&f.inputs, mask, asserted);
            retraction_step_ms(&f, 1);
            SP_ASSERT_EQ_INT(complete ? GEAR_STATE_UP_LOCKED : GEAR_STATE_RETRACTING,
                             f.status.state);
            SP_ASSERT_EQ_INT(!complete, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
            SP_ASSERT_EQ_INT(0, f.status.alarms_latched);

            /* Restore completion so each next case exercises loss of completion. */
            set_retraction_lock_mask(&f.inputs, RETRACTION_UP_LOCK_MASK, asserted);
            retraction_step_ms(&f, 1);
            SP_ASSERT_EQ_INT(GEAR_STATE_UP_LOCKED, f.status.state);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        }

        retraction_step_ms(&f, 1);
        SP_ASSERT_EQ_INT(GEAR_STATE_UP_LOCKED, f.status.state);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
    }
}

SP_TEST(loss_of_up_completion_respects_retraction_permission, "REQ-005,REQ-003")
{
    const GearConfig config = gear_config_default();
    const float speeds[] = {config.min_retraction_airspeed_kt - 1.0f,
                            config.min_retraction_airspeed_kt,
                            config.min_retraction_airspeed_kt + 1.0f};
    const uint8_t validity_values[] = {0, 1, 2, UINT8_MAX};
    const unsigned incomplete_masks[] = {0x30, 0x28, 0x18, 0x39, 0x3a, 0x3c};

    for (size_t mask = 0; mask < sizeof incomplete_masks / sizeof incomplete_masks[0]; mask++) {
        for (size_t speed = 0; speed < sizeof speeds / sizeof speeds[0]; speed++) {
            for (unsigned wow = 0; wow < 4; wow++) {
                for (size_t valid = 0; valid < sizeof validity_values / sizeof validity_values[0];
                     valid++) {
                    GearFixture f;
                    const int permitted = speed >= 1 && wow == 0 && validity_values[valid] != 0;

                    fixture_init(&f);
                    fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                    fixture_set_gear(&f, FIXTURE_GEAR_UP);
                    f.inputs.gear_handle_down = 0;
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(GEAR_STATE_UP_LOCKED, f.status.state);
                    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);

                    set_retraction_lock_mask(&f.inputs, incomplete_masks[mask], 2);
                    f.inputs.airspeed_kt = speeds[speed];
                    f.inputs.airspeed_valid = validity_values[valid];
                    f.inputs.left_wow = (wow & 1U) != 0 ? 2 : 0;
                    f.inputs.right_wow = (wow & 2U) != 0 ? UINT8_MAX : 0;
                    retraction_step_ms(&f, 1);
                    SP_ASSERT_EQ_INT(permitted, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
                    SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
                    SP_ASSERT_EQ_INT(0, f.status.alarms_latched);

                    /* Removing inhibition must resume drive without another lever change. */
                    fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                    retraction_step_ms(&f, 1);
                    SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);

                    fixture_set_gear(&f, FIXTURE_GEAR_UP);
                    retraction_step_ms(&f, 1);
                    SP_ASSERT_EQ_INT(GEAR_STATE_UP_LOCKED, f.status.state);
                    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
                }
            }
        }
    }
}

SP_TEST(resumed_retraction_timeout_and_completion_boundaries, "REQ-005,REQ-003,REQ-007,REQ-008")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const unsigned incomplete_masks[] = {0, 0x07, 0x30, 0x28, 0x18, 0x39, 0x3a, 0x3c};
    const uint32_t elapsed_times[] = {config.normal_transit_timeout_ms - 1U,
                                      config.normal_transit_timeout_ms,
                                      config.normal_transit_timeout_ms + 1U};

    SP_ASSERT(config.normal_transit_timeout_ms > 1U);
    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    for (size_t mask = 0; mask < sizeof incomplete_masks / sizeof incomplete_masks[0]; mask++) {
        for (size_t boundary = 0; boundary < sizeof elapsed_times / sizeof elapsed_times[0];
             boundary++) {
            for (unsigned complete = 0; complete < 2; complete++) {
                for (unsigned inhibited = 0; inhibited < 2; inhibited++) {
                    GearFixture f;
                    const int timed_out = complete == 0 && boundary >= 1;
                    const GearState expected =
                        timed_out ? GEAR_STATE_FAULT
                                  : (complete != 0 ? GEAR_STATE_UP_LOCKED : GEAR_STATE_RETRACTING);

                    fixture_init(&f);
                    fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                    fixture_set_gear(&f, FIXTURE_GEAR_UP);
                    f.inputs.gear_handle_down = 0;
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(GEAR_STATE_UP_LOCKED, f.status.state);

                    set_retraction_lock_mask(&f.inputs, incomplete_masks[mask], 1);
                    retraction_step_ms(&f, 1);
                    SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);

                    f.inputs.airspeed_valid = inhibited != 0 ? 0 : 1;
                    if (complete != 0) {
                        fixture_set_gear(&f, FIXTURE_GEAR_UP);
                    }
                    retraction_step_ms(&f, elapsed_times[boundary]);
                    SP_ASSERT_EQ_INT(expected, f.status.state);
                    SP_ASSERT_EQ_INT(complete == 0 && inhibited == 0, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(timed_out, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(timed_out, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(timed_out, f.outputs.master_warning);

                    if (timed_out) {
                        /* Fault operation must continue drive once permission is restored. */
                        f.inputs.airspeed_valid = 1;
                        retraction_step_ms(&f, 1);
                        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
                        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & fault) != 0);
                        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

                        fixture_set_gear(&f, FIXTURE_GEAR_UP);
                        retraction_step_ms(&f, 1);
                        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
                        SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);

                        /* Completion loss still resumes retraction with the latch present. */
                        set_retraction_lock_mask(&f.inputs, incomplete_masks[mask], 1);
                        retraction_step_ms(&f, 1);
                        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
                        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);

                        f.inputs.left_wow = 2;
                        retraction_step_ms(&f, 1);
                        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
                        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                    }
                }
            }
        }
    }
}

SP_TEST(inhibited_retraction_reverses_with_fresh_timer_or_latched_fault,
        "REQ-005,REQ-003,REQ-004,REQ-007,REQ-008")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint8_t down_values[] = {1, 2, UINT8_MAX};

    SP_ASSERT(config.normal_transit_timeout_ms > 2U);

    for (unsigned faulted = 0; faulted < 2; faulted++) {
        for (unsigned down_complete = 0; down_complete < 2; down_complete++) {
            for (size_t value = 0; value < sizeof down_values / sizeof down_values[0]; value++) {
                GearFixture f;
                const uint32_t elapsed = faulted != 0 ? config.normal_transit_timeout_ms
                                                      : config.normal_transit_timeout_ms - 2U;
                const GearState expected =
                    faulted != 0
                        ? GEAR_STATE_FAULT
                        : (down_complete != 0 ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_EXTENDING);

                fixture_init(&f);
                fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
                f.inputs.gear_handle_down = 0;
                retraction_step_ms(&f, 1);
                SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);

                retraction_step_ms(&f, elapsed);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(1, f.outputs.up_valve);

                /* Stop retraction before reversing, including with a fault already latched. */
                f.inputs.airspeed_kt = config.min_retraction_airspeed_kt - 1.0f;
                f.inputs.airspeed_valid = 0;
                f.inputs.left_wow = 2;
                f.inputs.right_wow = UINT8_MAX;
                retraction_step_ms(&f, 1);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);

                if (down_complete != 0) {
                    fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
                }
                f.inputs.gear_handle_down = down_values[value];
                retraction_step_ms(&f, 1);
                SP_ASSERT_EQ_INT(expected, f.status.state);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(down_complete == 0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, f.outputs.master_warning);

                if (faulted == 0 && down_complete == 0) {
                    /* The old retraction timer would expire at reversal, but must be discarded. */
                    retraction_step_ms(&f, config.normal_transit_timeout_ms - 1U);
                    SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
                    SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);

                    retraction_step_ms(&f, 1);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
                    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
                    SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);

                    retraction_step_ms(&f, 1);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
                    SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                }
            }
        }
    }
}

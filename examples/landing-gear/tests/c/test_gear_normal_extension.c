#include "gear_fixture.h"
#include "sp_test.h"

enum { EXTENSION_DOWN_LOCK_MASK = 0x07, EXTENSION_LOCK_COMBINATIONS = 64 };

/* Bits 0..2 are nose/left/right downlocks; bits 3..5 are the uplocks. */
static void set_extension_lock_mask(GearInputs *inputs, unsigned mask, uint8_t asserted)
{
    inputs->nose_downlock = (mask & 0x01U) != 0 ? asserted : 0;
    inputs->left_downlock = (mask & 0x02U) != 0 ? asserted : 0;
    inputs->right_downlock = (mask & 0x04U) != 0 ? asserted : 0;
    inputs->nose_uplock = (mask & 0x08U) != 0 ? asserted : 0;
    inputs->left_uplock = (mask & 0x10U) != 0 ? asserted : 0;
    inputs->right_uplock = (mask & 0x20U) != 0 ? asserted : 0;
}

static void extension_step_ms(GearFixture *f, uint32_t dt_ms)
{
    const GearCommands commands = {0};

    gear_step(&f->controller, &f->inputs, &commands, dt_ms, &f->outputs);
    gear_get_status(&f->controller, &f->status);
}

SP_TEST(normal_extension_reverses_and_requires_three_downlocks, "REQ-004,REQ-005,REQ-006")
{
    const GearConfig config = gear_config_default();
    const uint8_t asserted_values[] = {1, 2, UINT8_MAX};

    for (size_t value = 0; value < sizeof asserted_values / sizeof asserted_values[0]; value++) {
        GearFixture f;
        const uint8_t asserted = asserted_values[value];

        fixture_init(&f);
        fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
        f.inputs.gear_handle_down = 0;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(GEAR_STATE_RETRACTING, f.status.state);
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);

        fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
        f.inputs.gear_handle_down = asserted;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(GEAR_STATE_EXTENDING, f.status.state);
        SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);

        f.inputs.nose_downlock = asserted;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);

        /* Two locks are just below completion; exactly three complete extension. */
        f.inputs.left_downlock = asserted;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(GEAR_STATE_EXTENDING, f.status.state);
        SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);

        f.inputs.right_downlock = asserted;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(GEAR_STATE_DOWN_LOCKED, f.status.state);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.status.alarms_latched);

        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);

        /* Zero, unlike every tested nonzero lever value, selects retraction. */
        f.inputs.gear_handle_down = 0;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
    }
}

SP_TEST(completed_extension_rechecks_every_lock_combination, "REQ-004")
{
    const uint8_t asserted_values[] = {1, 2, UINT8_MAX};

    for (size_t value = 0; value < sizeof asserted_values / sizeof asserted_values[0]; value++) {
        GearFixture f;

        fixture_init(&f);
        fixture_step(&f);
        SP_ASSERT_EQ_INT(GEAR_STATE_DOWN_LOCKED, f.status.state);

        for (unsigned mask = 0; mask < EXTENSION_LOCK_COMBINATIONS; mask++) {
            const int complete = mask == EXTENSION_DOWN_LOCK_MASK;

            /* Begin each case already completed, rather than in INIT. */
            set_extension_lock_mask(&f.inputs, mask, asserted_values[value]);
            fixture_step(&f);
            SP_ASSERT_EQ_INT(complete ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_EXTENDING,
                             f.status.state);
            SP_ASSERT_EQ_INT(!complete, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
            SP_ASSERT_EQ_INT(0, f.status.alarms_latched);

            /* Includes all three downlocks plus each contradictory uplock. */
            set_extension_lock_mask(&f.inputs, EXTENSION_DOWN_LOCK_MASK, asserted_values[value]);
            fixture_step(&f);
            SP_ASSERT_EQ_INT(GEAR_STATE_DOWN_LOCKED, f.status.state);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
        }
    }
}

SP_TEST(ongoing_extension_ignores_speed_validity_and_weight_on_wheels, "REQ-004")
{
    const GearConfig config = gear_config_default();
    const float speeds[] = {
        0.0f, config.min_retraction_airspeed_kt - 1.0f, config.min_retraction_airspeed_kt,
        config.min_retraction_airspeed_kt + 1.0f, config.gear_warning_airspeed_kt + 1.0f};
    const uint8_t validity_values[] = {0, 1, 2, UINT8_MAX};

    for (size_t speed = 0; speed < sizeof speeds / sizeof speeds[0]; speed++) {
        for (unsigned wow = 0; wow < 4; wow++) {
            for (size_t valid = 0; valid < sizeof validity_values / sizeof validity_values[0];
                 valid++) {
                GearFixture f;

                fixture_init(&f);
                fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                fixture_set_gear(&f, FIXTURE_GEAR_UP);
                fixture_step(&f);
                SP_ASSERT_EQ_INT(GEAR_STATE_EXTENDING, f.status.state);
                SP_ASSERT_EQ_INT(1, f.outputs.down_valve);

                /* Change the interlock inputs during an already-started extension. */
                fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
                f.inputs.airspeed_kt = speeds[speed];
                f.inputs.airspeed_valid = validity_values[valid];
                f.inputs.left_wow = (wow & 1U) != 0 ? 2 : 0;
                f.inputs.right_wow = (wow & 2U) != 0 ? UINT8_MAX : 0;
                fixture_step(&f);
                SP_ASSERT_EQ_INT(GEAR_STATE_EXTENDING, f.status.state);
                SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
                SP_ASSERT_EQ_INT(0, f.outputs.master_warning);

                fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
                fixture_step(&f);
                SP_ASSERT_EQ_INT(GEAR_STATE_DOWN_LOCKED, f.status.state);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
            }
        }
    }
}

SP_TEST(extension_timeout_alarm_and_completion_boundaries, "REQ-004,REQ-007,REQ-008")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const unsigned incomplete_masks[] = {0, 0x38, 0x06, 0x05, 0x03, 0x0f, 0x17, 0x27};
    const uint32_t elapsed_times[] = {config.normal_transit_timeout_ms - 1U,
                                      config.normal_transit_timeout_ms,
                                      config.normal_transit_timeout_ms + 1U};

    SP_ASSERT(config.normal_transit_timeout_ms > 1U);
    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    for (size_t mask = 0; mask < sizeof incomplete_masks / sizeof incomplete_masks[0]; mask++) {
        for (size_t boundary = 0; boundary < sizeof elapsed_times / sizeof elapsed_times[0];
             boundary++) {
            for (unsigned complete = 0; complete < 2; complete++) {
                GearFixture f;
                const int timed_out = complete == 0 && boundary >= 1;
                const GearState expected =
                    timed_out ? GEAR_STATE_FAULT
                              : (complete != 0 ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_EXTENDING);

                fixture_init(&f);
                fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                fixture_step(&f);
                SP_ASSERT_EQ_INT(GEAR_STATE_DOWN_LOCKED, f.status.state);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);

                /* A missing downlock or contradictory uplock resumes extension. */
                set_extension_lock_mask(&f.inputs, incomplete_masks[mask], 1);
                fixture_step(&f);
                SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);

                if (complete != 0) {
                    fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
                }
                extension_step_ms(&f, elapsed_times[boundary]);
                SP_ASSERT_EQ_INT(expected, f.status.state);
                SP_ASSERT_EQ_INT(complete == 0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(timed_out, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(timed_out, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(timed_out, f.outputs.master_warning);

                if (timed_out) {
                    /* The alarm must not interrupt extension, including above the limit. */
                    extension_step_ms(&f, 1);
                    SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

                    fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
                    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);

                    /* A latched fault also must not inhibit a later loss of completion. */
                    set_extension_lock_mask(&f.inputs, incomplete_masks[mask], 1);
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(1, f.outputs.down_valve);
                    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
                }
            }
        }
    }
}

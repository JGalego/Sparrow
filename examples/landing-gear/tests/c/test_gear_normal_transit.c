#include "gear_fixture.h"
#include "sp_test.h"

static void normal_transit_step_ms(GearFixture *f, uint32_t dt_ms)
{
    gear_step(&f->controller, &f->inputs, NULL, dt_ms, &f->outputs);
    gear_get_status(&f->controller, &f->status);
}

static SpStatus normal_transit_fixture_init(GearFixture *f, const GearConfig *config,
                                            unsigned handle_down)
{
    SpStatus status;

    fixture_init(f);
    status = gear_init(&f->controller, config, NULL);
    if (status != SP_OK) {
        return status;
    }
    fixture_fly(f, 3000.0f, config->min_retraction_airspeed_kt);
    fixture_set_gear(f, FIXTURE_GEAR_TRANSIT);
    f->inputs.gear_handle_down = (uint8_t)handle_down;
    return SP_OK;
}

static int normal_transit_alarm_matches(const GearFixture *f, int raised)
{
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);

    return ((f->status.alarms_active & fault) != 0) == raised &&
           ((f->status.alarms_latched & fault) != 0) == raised &&
           ((f->status.alarms_unacked & fault) != 0) == raised &&
           f->outputs.master_warning == raised && (!raised || f->status.state == GEAR_STATE_FAULT);
}

static int normal_transit_drive_matches(const GearFixture *f, unsigned handle_down)
{
    return f->outputs.down_valve == (handle_down != 0) &&
           f->outputs.up_valve == (handle_down == 0) && f->outputs.uplock_release == 0;
}

SP_TEST(configured_transit_timeout_starts_at_first_drive_and_accumulates_steps, "REQ-007")
{
    GearConfig config = gear_config_default();
    const uint32_t default_timeout = config.normal_transit_timeout_ms;
    const GearFaultInfo *info = gear_fault_info(GEAR_FAULT_GEAR_DISAGREE);

    SP_ASSERT(default_timeout > 2U);
    SP_ASSERT(default_timeout < UINT32_MAX);
    SP_ASSERT(info != NULL);
    SP_ASSERT_EQ_INT(GEAR_SEVERITY_CRITICAL, info->severity);
    SP_ASSERT((GEAR_CRITICAL_FAULT_MASK & GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE)) != 0);

    /* Use an accepted non-default value without assuming the model's range endpoints. */
    config.normal_transit_timeout_ms = default_timeout + 1U;
    if (gear_config_validate(&config, NULL) != SP_OK) {
        config.normal_transit_timeout_ms = default_timeout - 1U;
    }
    SP_ASSERT_EQ_INT(SP_OK, gear_config_validate(&config, NULL));
    SP_ASSERT(config.normal_transit_timeout_ms != default_timeout);
    SP_ASSERT(config.normal_transit_timeout_ms > 2U);
    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    for (unsigned down = 0; down < 2; down++) {
        GearFixture f;
        const uint32_t first_interval = config.normal_transit_timeout_ms / 2U;
        const uint32_t intervals[] = {config.normal_transit_timeout_ms - 1U - first_interval, 1U,
                                      1U};

        SP_ASSERT_EQ_INT(SP_OK, normal_transit_fixture_init(&f, &config, down));
        /* Even a very late first step starts the timer at the newly issued command. */
        normal_transit_step_ms(&f, UINT32_MAX);
        SP_ASSERT(normal_transit_drive_matches(&f, down));
        SP_ASSERT(normal_transit_alarm_matches(&f, 0));
        SP_ASSERT_EQ_INT(down != 0 ? GEAR_STATE_EXTENDING : GEAR_STATE_RETRACTING, f.status.state);

        normal_transit_step_ms(&f, first_interval);
        SP_ASSERT(normal_transit_alarm_matches(&f, 0));
        /* Unequal steps reach configured timeout - 1, timeout, and timeout + 1 ms. */
        for (size_t boundary = 0; boundary < sizeof intervals / sizeof intervals[0]; boundary++) {
            normal_transit_step_ms(&f, intervals[boundary]);
            SP_ASSERT(normal_transit_alarm_matches(&f, boundary != 0));
            SP_ASSERT(normal_transit_drive_matches(&f, down));
        }
    }
}

SP_TEST(reversal_to_inhibited_retraction_waits_for_drive_before_starting_fresh_timer, "REQ-007")
{
    const GearConfig config = gear_config_default();
    const uint32_t intervals[] = {config.normal_transit_timeout_ms - 1U, 1U, 1U};
    GearFixture f;

    SP_ASSERT(config.normal_transit_timeout_ms > 2U);
    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);
    SP_ASSERT_EQ_INT(SP_OK, normal_transit_fixture_init(&f, &config, 1));
    normal_transit_step_ms(&f, 1);
    normal_transit_step_ms(&f, config.normal_transit_timeout_ms - 1U);
    SP_ASSERT(normal_transit_drive_matches(&f, 1));
    SP_ASSERT(normal_transit_alarm_matches(&f, 0));

    /* Reverse at the old timer's deadline, but deny drive in the new direction. */
    f.inputs.gear_handle_down = 0;
    f.inputs.airspeed_valid = 0;
    normal_transit_step_ms(&f, 1);
    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
    SP_ASSERT_EQ_INT(GEAR_STATE_RETRACTING, f.status.state);
    SP_ASSERT(normal_transit_alarm_matches(&f, 0));

    normal_transit_step_ms(&f, config.normal_transit_timeout_ms + 1U);
    SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
    SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
    SP_ASSERT(normal_transit_alarm_matches(&f, 0));

    /* Neither the old extension nor the inhibited interval may age the new timer. */
    f.inputs.airspeed_valid = 1;
    normal_transit_step_ms(&f, config.normal_transit_timeout_ms + 1U);
    SP_ASSERT(normal_transit_drive_matches(&f, 0));
    SP_ASSERT(normal_transit_alarm_matches(&f, 0));

    for (size_t boundary = 0; boundary < sizeof intervals / sizeof intervals[0]; boundary++) {
        normal_transit_step_ms(&f, intervals[boundary]);
        SP_ASSERT(normal_transit_alarm_matches(&f, boundary != 0));
        SP_ASSERT(normal_transit_drive_matches(&f, 0));
    }
}

SP_TEST(alternate_cancels_near_deadline_and_release_gets_a_full_fresh_timeout, "REQ-007")
{
    const GearConfig config = gear_config_default();
    const uint32_t intervals[] = {config.normal_transit_timeout_ms - 1U, 1U, 1U};

    SP_ASSERT(config.normal_transit_timeout_ms > 2U);
    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    for (unsigned down = 0; down < 2; down++) {
        for (uint32_t cancellation_interval = 1; cancellation_interval <= 3;
             cancellation_interval++) {
            GearFixture f;

            SP_ASSERT_EQ_INT(SP_OK, normal_transit_fixture_init(&f, &config, down));
            normal_transit_step_ms(&f, 1);
            normal_transit_step_ms(&f, config.normal_transit_timeout_ms - 2U);
            SP_ASSERT(normal_transit_alarm_matches(&f, 0));

            /* Without cancellation, elapsed time would be timeout - 1, at, or + 1. */
            f.inputs.alternate_extend = UINT8_MAX;
            normal_transit_step_ms(&f, cancellation_interval);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
            SP_ASSERT_EQ_INT(GEAR_STATE_ALTERNATE_EXTENDING, f.status.state);
            SP_ASSERT(normal_transit_alarm_matches(&f, 0));

            normal_transit_step_ms(&f, UINT32_MAX);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
            SP_ASSERT(normal_transit_alarm_matches(&f, 0));

            /* Keep the same lever direction so cancellation cannot rely on reversal. */
            f.inputs.alternate_extend = 0;
            normal_transit_step_ms(&f, config.normal_transit_timeout_ms + 1U);
            SP_ASSERT(normal_transit_drive_matches(&f, down));
            SP_ASSERT(normal_transit_alarm_matches(&f, 0));

            for (size_t boundary = 0; boundary < sizeof intervals / sizeof intervals[0];
                 boundary++) {
                normal_transit_step_ms(&f, intervals[boundary]);
                SP_ASSERT(normal_transit_alarm_matches(&f, boundary != 0));
                SP_ASSERT(normal_transit_drive_matches(&f, down));
            }
        }
    }
}

SP_TEST(nonzero_lever_value_changes_do_not_restart_extension_timer, "REQ-007")
{
    const GearConfig config = gear_config_default();
    const uint8_t down_values[] = {2, UINT8_MAX, 1};
    GearFixture f;

    SP_ASSERT(config.normal_transit_timeout_ms > 2U);
    SP_ASSERT_EQ_INT(SP_OK, normal_transit_fixture_init(&f, &config, 1));
    normal_transit_step_ms(&f, 1);
    normal_transit_step_ms(&f, config.normal_transit_timeout_ms - 2U);
    SP_ASSERT(normal_transit_alarm_matches(&f, 0));

    /* All nonzero values still select down: these are not direction changes. */
    for (size_t boundary = 0; boundary < sizeof down_values / sizeof down_values[0]; boundary++) {
        f.inputs.gear_handle_down = down_values[boundary];
        normal_transit_step_ms(&f, 1);
        SP_ASSERT(normal_transit_alarm_matches(&f, boundary != 0));
        SP_ASSERT(normal_transit_drive_matches(&f, 1));
    }
}

SP_TEST(completion_cancels_timer_and_later_lock_loss_starts_a_fresh_transit, "REQ-007")
{
    const GearConfig config = gear_config_default();
    const uint32_t intervals[] = {config.normal_transit_timeout_ms - 1U, 1U, 1U};

    SP_ASSERT(config.normal_transit_timeout_ms > 2U);
    SP_ASSERT(config.normal_transit_timeout_ms < UINT32_MAX);

    for (unsigned down = 0; down < 2; down++) {
        GearFixture f;

        SP_ASSERT_EQ_INT(SP_OK, normal_transit_fixture_init(&f, &config, down));
        normal_transit_step_ms(&f, 1);
        normal_transit_step_ms(&f, config.normal_transit_timeout_ms / 2U);
        SP_ASSERT(normal_transit_alarm_matches(&f, 0));

        fixture_set_gear(&f, down != 0 ? FIXTURE_GEAR_DOWN : FIXTURE_GEAR_UP);
        normal_transit_step_ms(&f, 1);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
        SP_ASSERT(normal_transit_alarm_matches(&f, 0));
        normal_transit_step_ms(&f, UINT32_MAX);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
        SP_ASSERT(normal_transit_alarm_matches(&f, 0));

        /* A new incomplete position must not inherit elapsed time from the old transit. */
        fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
        normal_transit_step_ms(&f, config.normal_transit_timeout_ms + 1U);
        SP_ASSERT(normal_transit_drive_matches(&f, down));
        SP_ASSERT(normal_transit_alarm_matches(&f, 0));

        for (size_t boundary = 0; boundary < sizeof intervals / sizeof intervals[0]; boundary++) {
            normal_transit_step_ms(&f, intervals[boundary]);
            SP_ASSERT(normal_transit_alarm_matches(&f, boundary != 0));
            SP_ASSERT(normal_transit_drive_matches(&f, down));
        }
    }
}

SP_TEST(large_elapsed_interval_cannot_wrap_transit_time_and_hide_timeout, "REQ-007")
{
    const GearConfig config = gear_config_default();

    SP_ASSERT(config.normal_transit_timeout_ms > 2U);

    for (unsigned down = 0; down < 2; down++) {
        GearFixture f;

        SP_ASSERT_EQ_INT(SP_OK, normal_transit_fixture_init(&f, &config, down));
        normal_transit_step_ms(&f, 1);
        normal_transit_step_ms(&f, config.normal_transit_timeout_ms - 1U);
        SP_ASSERT(normal_transit_alarm_matches(&f, 0));

        /* Wrapping addition would produce timeout - 2 and incorrectly miss the fault. */
        normal_transit_step_ms(&f, UINT32_MAX);
        SP_ASSERT(normal_transit_alarm_matches(&f, 1));
        SP_ASSERT(normal_transit_drive_matches(&f, down));
    }
}

#include "gear_fixture.h"
#include "sp_test.h"

static void step_elapsed(GearFixture *f, uint32_t dt_ms)
{
    const GearCommands commands = {0};

    gear_step(&f->controller, &f->inputs, &commands, dt_ms, &f->outputs);
    gear_get_status(&f->controller, &f->status);
}

static SpStatus start_retraction(GearFixture *f, const GearConfig *config, int faulted)
{
    SpStatus status;

    fixture_init(f);
    status = gear_init(&f->controller, config, NULL);
    if (status != SP_OK) {
        return status;
    }
    fixture_fly(f, 3000.0f, config->min_retraction_airspeed_kt);
    f->inputs.gear_handle_down = 0;
    step_elapsed(f, 1);
    if (faulted) {
        /* Leave the sensed gear down so the started retraction cannot complete. */
        step_elapsed(f, config->normal_transit_timeout_ms);
    }
    return SP_OK;
}

/* Causes: low speed, invalid speed, left WOW, right WOW, and both WOW inputs. */
static void inhibit_retraction(GearFixture *f, const GearConfig *config, unsigned cause)
{
    f->inputs.airspeed_kt = config->min_retraction_airspeed_kt;
    if (cause == 0) {
        f->inputs.airspeed_kt -= 1.0f;
    }
    f->inputs.airspeed_valid = cause == 1 ? 0 : 1;
    f->inputs.left_wow = (cause == 2 || cause == 4) ? 1 : 0;
    f->inputs.right_wow = (cause == 3 || cause == 4) ? 1 : 0;
}

SP_TEST(ongoing_retraction_uses_configured_speed_boundary, "REQ-003")
{
    GearConfig config = gear_config_default();
    const float default_minimum = config.min_retraction_airspeed_kt;
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);

    /* Select a nearby accepted value without assuming the model's range endpoints. */
    config.min_retraction_airspeed_kt = default_minimum + 1.0f;
    if (gear_config_validate(&config, NULL) != SP_OK) {
        config.min_retraction_airspeed_kt = default_minimum - 1.0f;
    }
    SP_ASSERT_EQ_INT(SP_OK, gear_config_validate(&config, NULL));
    SP_ASSERT(config.min_retraction_airspeed_kt != default_minimum);
    SP_ASSERT(config.normal_transit_timeout_ms > 2);

    for (int faulted = 0; faulted <= 1; faulted++) {
        for (int offset = -1; offset <= 1; offset++) {
            GearFixture f;
            const GearState expected_state = faulted ? GEAR_STATE_FAULT : GEAR_STATE_RETRACTING;

            SP_ASSERT_EQ_INT(SP_OK, start_retraction(&f, &config, faulted));
            SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(expected_state, f.status.state);

            f.inputs.airspeed_kt = config.min_retraction_airspeed_kt + (float)offset;
            step_elapsed(&f, 1);

            SP_ASSERT_EQ_INT(offset >= 0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
            SP_ASSERT_EQ_INT(expected_state, f.status.state);
            SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(faulted, f.outputs.master_warning);

            /* Permission recovery at equality must work without reset or reinitialization. */
            f.inputs.airspeed_kt = config.min_retraction_airspeed_kt;
            step_elapsed(&f, 1);
            SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(expected_state, f.status.state);
            SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
        }
    }
}

SP_TEST(ongoing_retraction_requires_both_wow_zero_and_valid_speed, "REQ-003")
{
    const GearConfig config = gear_config_default();
    const uint8_t validity_values[] = {0, 1, 2, UINT8_MAX};
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);

    SP_ASSERT(config.normal_transit_timeout_ms > 2);
    for (int faulted = 0; faulted <= 1; faulted++) {
        for (unsigned wow = 0; wow < 4; wow++) {
            for (size_t valid = 0; valid < sizeof validity_values / sizeof validity_values[0];
                 valid++) {
                GearFixture f;
                const GearState expected_state = faulted ? GEAR_STATE_FAULT : GEAR_STATE_RETRACTING;

                SP_ASSERT_EQ_INT(SP_OK, start_retraction(&f, &config, faulted));
                SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(expected_state, f.status.state);

                f.inputs.left_wow = (wow & 1U) != 0 ? 2 : 0;
                f.inputs.right_wow = (wow & 2U) != 0 ? UINT8_MAX : 0;
                f.inputs.airspeed_valid = validity_values[valid];
                step_elapsed(&f, 1);

                SP_ASSERT_EQ_INT(wow == 0 && validity_values[valid] != 0, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.uplock_release);
                SP_ASSERT_EQ_INT(expected_state, f.status.state);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, f.outputs.master_warning);

                fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
                step_elapsed(&f, 1);
                SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
                SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
                SP_ASSERT_EQ_INT(expected_state, f.status.state);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
            }
        }
    }
}

SP_TEST(inhibition_before_drive_does_not_raise_disagree, "REQ-003,REQ-007")
{
    const GearConfig config = gear_config_default();
    const uint32_t intervals[] = {config.normal_transit_timeout_ms - 1, 1, 1};

    SP_ASSERT(config.normal_transit_timeout_ms > 1);
    for (unsigned cause = 0; cause < 5; cause++) {
        GearFixture f;

        fixture_init(&f);
        fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
        f.inputs.gear_handle_down = 0;
        inhibit_retraction(&f, &config, cause);

        /* Pass below, at, and beyond the timeout without ever starting hydraulic drive. */
        for (size_t boundary = 0; boundary < sizeof intervals / sizeof intervals[0]; boundary++) {
            step_elapsed(&f, intervals[boundary]);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(0, f.status.alarms_active);
            SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
            SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
            SP_ASSERT_EQ_INT(GEAR_STATE_RETRACTING, f.status.state);
        }

        /* The preceding inhibited time must not cause an immediate fault on permission recovery. */
        fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
        step_elapsed(&f, 1);
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.status.alarms_active);
        SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
        SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
        SP_ASSERT_EQ_INT(GEAR_STATE_RETRACTING, f.status.state);
    }
}

SP_TEST(started_transit_times_out_without_bypassing_interlock, "REQ-003,REQ-007,REQ-008")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint32_t intervals[] = {config.normal_transit_timeout_ms - 1, 1, 1};

    SP_ASSERT(config.normal_transit_timeout_ms > 1);
    for (unsigned cause = 0; cause < 5; cause++) {
        GearFixture f;

        SP_ASSERT_EQ_INT(SP_OK, start_retraction(&f, &config, 0));
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.status.alarms_active);
        SP_ASSERT_EQ_INT(0, f.status.alarms_latched);
        inhibit_retraction(&f, &config, cause);

        /* Loss of permission stops the valve, but not an already-started transit timer. */
        for (size_t boundary = 0; boundary < sizeof intervals / sizeof intervals[0]; boundary++) {
            const int timed_out = boundary != 0;

            step_elapsed(&f, intervals[boundary]);
            SP_ASSERT_EQ_INT(0, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
            SP_ASSERT_EQ_INT(timed_out, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(timed_out, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(timed_out, f.outputs.master_warning);
            SP_ASSERT_EQ_INT(timed_out ? GEAR_STATE_FAULT : GEAR_STATE_RETRACTING, f.status.state);
        }

        /* Restored permission permits recovery drive, without clearing the fault latch. */
        fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
        step_elapsed(&f, 1);
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
        SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
    }
}

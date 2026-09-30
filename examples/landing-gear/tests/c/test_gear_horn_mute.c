#include "gear_fixture.h"
#include "sp_test.h"

static void horn_step(GearFixture *f, const GearCommands *commands)
{
    gear_step(&f->controller, &f->inputs, commands, 1U, &f->outputs);
    gear_get_status(&f->controller, &f->status);
}

static void horn_set_warning_inputs(GearFixture *f, const GearConfig *config)
{
    fixture_set_gear(f, FIXTURE_GEAR_UP);
    f->inputs.gear_handle_down = 0;
    fixture_fly(f, config->horn_mute_min_altitude_ft + 1.0f,
                config->gear_warning_airspeed_kt - 1.0f);
}

SP_TEST(horn_initialization_clears_previous_episode_mute, "REQ-010,REQ-011,REQ-014")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands mute = {.mute = 1};
    GearFixture f;

    SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);
    fixture_init(&f);
    fixture_arm_warning(&f, &config);
    horn_set_warning_inputs(&f, &config);

    /* A request on the first warning step must take effect on that same step. */
    horn_step(&f, &mute);
    SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
    SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

    /* Reinitialization clears both arming and the previous episode's mute. */
    SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
    horn_step(&f, NULL);
    SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
    SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

    fixture_arm_warning(&f, &config);
    horn_set_warning_inputs(&f, &config);
    horn_step(&f, NULL);
    SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
    SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);
}

SP_TEST(horn_mute_retention_uses_configured_strict_altitude_limit, "REQ-011,REQ-012")
{
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands mute = {.mute = 2};
    const GearCommands none = {0};

    for (unsigned configured = 0; configured < 2; configured++) {
        GearConfig config = gear_config_default();

        /* Exercise the default and a changed limit to detect a hard-coded 200 ft. */
        config.horn_mute_min_altitude_ft += (float)configured;
        SP_ASSERT(config.horn_mute_min_altitude_ft > 1.0f);
        SP_ASSERT(config.horn_mute_min_altitude_ft + 2.0f < config.gear_warning_altitude_ft);
        SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

        for (int offset = -1; offset <= 1; offset++) {
            GearFixture f;

            fixture_init(&f);
            SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
            fixture_arm_warning(&f, &config);
            horn_set_warning_inputs(&f, &config);
            horn_step(&f, NULL);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);

            horn_step(&f, &mute);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

            /* Mute survives an altitude change without a repeated request. */
            f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
            horn_step(&f, &none);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

            /* Descend just below, to, or just above the configured mute limit. */
            f.inputs.radio_altitude_ft = config.horn_mute_min_altitude_ft + (float)offset;
            horn_step(&f, NULL);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);

            /* A fresh request cannot override cancellation at or below the limit. */
            horn_step(&f, &mute);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);

            /* Climbing does not restore a canceled mute. */
            f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
            horn_step(&f, &none);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);

            horn_step(&f, &mute);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);
        }
    }
}

SP_TEST(horn_request_cannot_pre_mute_at_warning_thresholds, "REQ-010,REQ-011")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands mute = {.mute = UINT8_MAX};
    const GearCommands none = {0};

    SP_ASSERT(config.gear_warning_altitude_ft - 1.0f > config.horn_mute_min_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

    for (unsigned limit = 0; limit < 2; limit++) {
        for (int offset = -1; offset <= 1; offset++) {
            const int condition = offset < 0;
            GearFixture f;

            fixture_init(&f);
            fixture_arm_warning(&f, &config);
            horn_set_warning_inputs(&f, &config);
            if (limit == 0) {
                f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft + (float)offset;
            } else {
                f.inputs.airspeed_kt = config.gear_warning_airspeed_kt + (float)offset;
            }

            /* Show both alarm assertion and absence before requesting mute. */
            horn_step(&f, NULL);
            SP_ASSERT_EQ_INT(condition, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(condition, f.outputs.gear_horn);

            horn_step(&f, &mute);
            SP_ASSERT_EQ_INT(condition, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

            /* Only a request received during a warning may persist into this step. */
            horn_set_warning_inputs(&f, &config);
            horn_step(&f, &none);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(!condition, f.outputs.gear_horn);
        }
    }
}

SP_TEST(horn_absent_warning_rejects_requests_and_discards_episode_mute, "REQ-011,REQ-013")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands mute = {.mute = 1};
    const GearCommands none = {0};

    SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

    for (unsigned absent = 0; absent < 3; absent++) {
        GearFixture f;
        GearInputs absent_inputs;

        fixture_init(&f);
        fixture_arm_warning(&f, &config);
        horn_set_warning_inputs(&f, &config);
        if (absent == 0) {
            f.inputs.radio_altitude_valid = 0;
        } else if (absent == 1) {
            f.inputs.airspeed_valid = 0;
        } else {
            fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
        }
        absent_inputs = f.inputs;

        /* No warning has occurred yet: this request must not pre-mute one. */
        horn_step(&f, &mute);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        horn_set_warning_inputs(&f, &config);
        horn_step(&f, NULL);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);

        horn_step(&f, &mute);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        /* Ending a muted episode must clear memory, even with another request. */
        f.inputs = absent_inputs;
        horn_step(&f, &mute);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        horn_set_warning_inputs(&f, &config);
        horn_step(&f, &none);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);
    }
}

SP_TEST(horn_descent_cancellation_precedes_same_step_mute, "REQ-012")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands mute = {.mute = UINT8_MAX};

    SP_ASSERT(config.horn_mute_min_altitude_ft > 1.0f);
    SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

    for (int offset = -1; offset <= 1; offset++) {
        GearFixture f;

        fixture_init(&f);
        fixture_arm_warning(&f, &config);
        horn_set_warning_inputs(&f, &config);
        horn_step(&f, &mute);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        /* Unlike sequential rejection, this request arrives in the descent step. */
        f.inputs.radio_altitude_ft = config.horn_mute_min_altitude_ft + (float)offset;
        horn_step(&f, &mute);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);

        /* The rejected request must not leave a hidden mute that reappears on climb. */
        f.inputs.radio_altitude_ft = config.horn_mute_min_altitude_ft + 1.0f;
        horn_step(&f, NULL);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);
    }
}

SP_TEST(horn_warning_onset_enforces_low_altitude_mute_limit, "REQ-012")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands mute = {.mute = 1};

    SP_ASSERT(config.horn_mute_min_altitude_ft > 1.0f);
    SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

    for (int offset = -1; offset <= 1; offset++) {
        for (unsigned absent = 0; absent < 3; absent++) {
            GearFixture f;

            fixture_init(&f);
            fixture_arm_warning(&f, &config);
            horn_set_warning_inputs(&f, &config);
            f.inputs.radio_altitude_ft = config.horn_mute_min_altitude_ft + (float)offset;
            if (absent == 0) {
                f.inputs.radio_altitude_valid = 0;
            } else if (absent == 1) {
                f.inputs.airspeed_valid = 0;
            } else {
                fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
            }

            /* Low altitude alone must not assert either the warning or the horn. */
            horn_step(&f, &mute);
            SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

            /* Restore the condition with a fresh request in its first active step. */
            horn_set_warning_inputs(&f, &config);
            f.inputs.radio_altitude_ft = config.horn_mute_min_altitude_ft + (float)offset;
            horn_step(&f, &mute);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);

            horn_step(&f, NULL);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
            SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);
        }
    }
}

SP_TEST(horn_active_episode_ends_and_rearms_at_warning_thresholds, "REQ-013")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands mute = {.mute = 1};
    const GearCommands none = {0};

    SP_ASSERT(config.gear_warning_altitude_ft - 1.0f > config.horn_mute_min_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

    for (unsigned limit = 0; limit < 2; limit++) {
        for (int offset = -1; offset <= 1; offset++) {
            const int condition = offset < 0;
            const float altitude_ft = limit == 0 ? config.gear_warning_altitude_ft + (float)offset
                                                 : config.horn_mute_min_altitude_ft + 1.0f;
            const float airspeed_kt =
                config.gear_warning_airspeed_kt + (limit == 1 ? (float)offset : -1.0f);

            for (unsigned previous_muted = 0; previous_muted < 2; previous_muted++) {
                for (unsigned new_mute = 0; new_mute < 2; new_mute++) {
                    const GearCommands *initial = previous_muted != 0 ? &mute : NULL;
                    const GearCommands *recurrence = new_mute != 0 ? &mute : &none;
                    const int retained_mute = condition && previous_muted != 0;
                    const int expected_horn = !retained_mute && new_mute == 0;
                    GearFixture f;

                    fixture_init(&f);
                    fixture_arm_warning(&f, &config);
                    horn_set_warning_inputs(&f, &config);
                    horn_step(&f, initial);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                    SP_ASSERT_EQ_INT(previous_muted == 0, f.outputs.gear_horn);

                    /* Cross a limit from an active, possibly muted warning episode. */
                    fixture_fly(&f, altitude_ft, airspeed_kt);
                    horn_step(&f, NULL);
                    SP_ASSERT_EQ_INT(condition, (f.status.alarms_active & warning) != 0);
                    SP_ASSERT_EQ_INT(condition && previous_muted == 0, f.outputs.gear_horn);

                    /* Below the limit is still the same episode; at/above ends it. */
                    horn_set_warning_inputs(&f, &config);
                    horn_step(&f, recurrence);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                    SP_ASSERT_EQ_INT(expected_horn, f.outputs.gear_horn);

                    /* Rearming and a fresh permitted mute must not be delayed a step. */
                    horn_step(&f, NULL);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                    SP_ASSERT_EQ_INT(expected_horn, f.outputs.gear_horn);
                }
            }
        }
    }
}

SP_TEST(horn_sounding_episode_ends_on_invalid_data_or_downlock, "REQ-013")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands mute = {.mute = 1};

    SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

    for (unsigned absent = 0; absent < 3; absent++) {
        GearFixture f;

        fixture_init(&f);
        fixture_arm_warning(&f, &config);
        horn_set_warning_inputs(&f, &config);
        horn_step(&f, NULL);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);

        if (absent == 0) {
            f.inputs.radio_altitude_valid = 0;
        } else if (absent == 1) {
            f.inputs.airspeed_valid = 0;
        } else {
            fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
        }

        /* Unlike termination of an already-muted episode, this must switch the horn off. */
        horn_step(&f, NULL);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        /* A new request on the first recurring-warning step must silence it immediately. */
        horn_set_warning_inputs(&f, &config);
        horn_step(&f, &mute);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        horn_step(&f, NULL);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);
    }
}

SP_TEST(horn_ground_disarming_ends_episode_and_discards_mute, "REQ-013,REQ-014")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands mute = {.mute = UINT8_MAX};
    const GearCommands none = {0};

    SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

    /* Exercise either ground input independently, and both together. */
    for (unsigned ground = 1; ground < 4; ground++) {
        for (unsigned previous_muted = 0; previous_muted < 2; previous_muted++) {
            for (unsigned new_mute = 0; new_mute < 2; new_mute++) {
                const GearCommands *initial = previous_muted != 0 ? &mute : NULL;
                const GearCommands *recurrence = new_mute != 0 ? &mute : &none;
                GearFixture f;

                fixture_init(&f);
                fixture_arm_warning(&f, &config);
                horn_set_warning_inputs(&f, &config);
                horn_step(&f, initial);
                SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(previous_muted == 0, f.outputs.gear_horn);

                /* Keep every other warning predicate true while ground disarms it. */
                f.inputs.left_wow = (ground & 1U) != 0 ? 2 : 0;
                f.inputs.right_wow = (ground & 2U) != 0 ? UINT8_MAX : 0;
                horn_step(&f, &mute);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

                /* Leaving the ground at low altitude cannot restore the old episode. */
                fixture_set_ground(&f, 0);
                horn_step(&f, &mute);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

                /* Qualification alone is not a warning and cannot accept a pre-mute. */
                f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft;
                horn_step(&f, &mute);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

                /* Only a fresh permitted request may silence the recurring warning. */
                horn_set_warning_inputs(&f, &config);
                horn_step(&f, recurrence);
                SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(new_mute == 0, f.outputs.gear_horn);

                horn_step(&f, NULL);
                SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(new_mute == 0, f.outputs.gear_horn);
            }
        }
    }
}

SP_TEST(horn_disarmed_and_arming_requests_cannot_pre_mute, "REQ-011,REQ-014")
{
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const uint8_t mute_values[] = {1, 2, UINT8_MAX};

    for (unsigned configured = 0; configured < 2; configured++) {
        GearConfig config = gear_config_default();

        config.horn_mute_min_altitude_ft += (float)configured;
        SP_ASSERT(config.horn_mute_min_altitude_ft > 1.0f);
        SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
        SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

        for (int offset = -1; offset <= 1; offset++) {
            const float altitude_ft = config.horn_mute_min_altitude_ft + (float)offset;

            for (size_t request = 0; request < sizeof mute_values / sizeof mute_values[0];
                 request++) {
                GearFixture f;
                const GearCommands mute = {.mute = mute_values[request]};

                fixture_init(&f);
                SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
                horn_set_warning_inputs(&f, &config);
                f.inputs.radio_altitude_ft = altitude_ft;

                /* Valid low inputs do not establish a warning before climb qualification. */
                horn_step(&f, &mute);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

                /* Qualification arms the warning but cannot accept a pre-mute request. */
                f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft;
                horn_step(&f, &mute);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

                /* The first approach warning must sound without a fresh request. */
                f.inputs.radio_altitude_ft = altitude_ft;
                horn_step(&f, NULL);
                SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);

                /* Only a request during the warning and strictly above the limit works. */
                horn_step(&f, &mute);
                SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);

                horn_step(&f, NULL);
                SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);
            }
        }
    }
}

SP_TEST(horn_low_altitude_override_survives_fault_and_alternate_extension, "REQ-012")
{
    const GearConfig config = gear_config_default();
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const uint32_t disagree = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const GearCommands mute = {.mute = 1};
    const GearCommands mute_reset = {.mute = UINT8_MAX, .reset = UINT8_MAX};

    SP_ASSERT(config.horn_mute_min_altitude_ft > 1.0f);
    SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);
    SP_ASSERT(config.normal_transit_timeout_ms > 0U);

    for (int offset = -1; offset <= 1; offset++) {
        GearFixture f;

        fixture_init(&f);
        fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
        /* Qualification also starts an incomplete, lever-down hydraulic transit. */
        fixture_arm_warning(&f, &config);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        gear_step(&f.controller, &f.inputs, NULL, config.normal_transit_timeout_ms, &f.outputs);
        gear_get_status(&f.controller, &f.status);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & disagree) != 0);
        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        fixture_fly(&f, config.horn_mute_min_altitude_ft + 1.0f,
                    config.gear_warning_airspeed_kt - 1.0f);
        horn_step(&f, &mute);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        /* Alternate priority and a rejected reset must not bypass the horn limit. */
        f.inputs.alternate_extend = 1;
        f.inputs.radio_altitude_ft = config.horn_mute_min_altitude_ft + (float)offset;
        horn_step(&f, &mute_reset);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & disagree) != 0);
        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
        SP_ASSERT_EQ_INT(1, f.outputs.uplock_release);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);

        /* Climbing with the fault and alternate extension still present cannot re-mute. */
        f.inputs.radio_altitude_ft = config.horn_mute_min_altitude_ft + 1.0f;
        horn_step(&f, NULL);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);

        horn_step(&f, &mute);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);
        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

        /* Low altitude and a latched fault alone must not establish the warning. */
        f.inputs.radio_altitude_ft = config.horn_mute_min_altitude_ft + (float)offset;
        fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
        horn_step(&f, NULL);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & disagree) != 0);
        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
    }
}

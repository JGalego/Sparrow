#include "gear_fixture.h"
#include "sp_test.h"

static int too_low_active(const GearFixture *f)
{
    return (f->status.alarms_active & GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR)) != 0;
}

/* A valid, airborne, gear-up sample; callers choose the altitude qualification. */
static void set_airborne_sample(GearFixture *f, const GearConfig *config, float altitude_ft)
{
    fixture_fly(f, altitude_ft, config->gear_warning_airspeed_kt - 1.0f);
    fixture_set_gear(f, FIXTURE_GEAR_UP);
    f->inputs.gear_handle_down = 0;
    f->inputs.alternate_extend = 0;
}

/* Create a real disagree latch through public steps, without editing controller state. */
static void time_out_extension(GearFixture *f, const GearConfig *config)
{
    fixture_set_gear(f, FIXTURE_GEAR_TRANSIT);
    f->inputs.gear_handle_down = 1;
    f->inputs.alternate_extend = 0;
    fixture_step(f);
    gear_step(&f->controller, &f->inputs, NULL, config->normal_transit_timeout_ms, &f->outputs);
    gear_get_status(&f->controller, &f->status);
}

SP_TEST(warning_arming_initialization_discards_flight_history, "REQ-014")
{
    GearFixture f;
    const GearConfig config = gear_config_default();
    const float low = config.gear_warning_altitude_ft - 1.0f;

    fixture_init(&f);
    gear_get_status(&f.controller, &f.status);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    set_airborne_sample(&f, &config, low);
    fixture_step(&f);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft;
    fixture_step(&f);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    f.inputs.radio_altitude_ft = low;
    fixture_step(&f);
    SP_ASSERT_EQ_INT(1, too_low_active(&f));
    SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);

    SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
    gear_get_status(&f.controller, &f.status);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    fixture_step(&f);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft;
    fixture_step(&f);
    f.inputs.radio_altitude_ft = low;
    fixture_step(&f);
    SP_ASSERT_EQ_INT(1, too_low_active(&f));
    SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);
}

SP_TEST(warning_arming_altitude_boundary_is_inclusive, "REQ-014")
{
    const float offsets[] = {-1.0f, 0.0f, 1.0f};

    /* Exercise both the default threshold and a non-default configuration. */
    for (unsigned configured = 0; configured < 2; ++configured) {
        GearConfig config = gear_config_default();

        config.gear_warning_altitude_ft += 100.0f * configured;
        for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
            GearFixture f;
            const int qualifies = offsets[i] >= 0.0f;

            fixture_init(&f);
            SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
            set_airborne_sample(&f, &config, config.gear_warning_altitude_ft + offsets[i]);
            fixture_step(&f);
            SP_ASSERT_EQ_INT(0, too_low_active(&f));
            SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

            /* One qualifying step must suffice; no extra dwell or second sample. */
            f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
            fixture_step(&f);
            SP_ASSERT_EQ_INT(qualifies, too_low_active(&f));
            SP_ASSERT_EQ_INT(qualifies, f.outputs.gear_horn);
        }
    }
}

SP_TEST(warning_arming_invalid_altitude_cannot_qualify, "REQ-014")
{
    const GearConfig config = gear_config_default();
    const float offsets[] = {-1.0f, 0.0f, 1.0f};

    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        GearFixture f;

        fixture_init(&f);
        set_airborne_sample(&f, &config, config.gear_warning_altitude_ft + offsets[i]);
        f.inputs.radio_altitude_valid = 0;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, too_low_active(&f));
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        set_airborne_sample(&f, &config, config.gear_warning_altitude_ft - 1.0f);
        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, too_low_active(&f));
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        /* Validity is a nonzero predicate, not an equality-to-one predicate. */
        f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft;
        f.inputs.radio_altitude_valid = 2;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, too_low_active(&f));
        f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(1, too_low_active(&f));
        SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);
    }
}

SP_TEST(warning_arming_survives_descent_and_invalid_altitude, "REQ-014")
{
    GearFixture f;
    const GearConfig config = gear_config_default();
    const float offsets[] = {-1.0f, 0.0f, 1.0f};

    fixture_init(&f);
    set_airborne_sample(&f, &config, config.gear_warning_altitude_ft);
    fixture_step(&f);
    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
    fixture_step(&f);
    SP_ASSERT_EQ_INT(1, too_low_active(&f));
    SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);

    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft + offsets[i];
        f.inputs.radio_altitude_valid = 0;
        fixture_step(&f);
        SP_ASSERT_EQ_INT(0, too_low_active(&f));
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

        /* Return below the threshold: this sample cannot itself requalify. */
        set_airborne_sample(&f, &config, config.gear_warning_altitude_ft - 1.0f);
        fixture_step(&f);
        SP_ASSERT_EQ_INT(1, too_low_active(&f));
        SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);
    }

    f.inputs.radio_altitude_ft = 0.0f;
    fixture_step(&f);
    SP_ASSERT_EQ_INT(1, too_low_active(&f));
    SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);
}

SP_TEST(warning_arming_ground_clears_history_and_takes_precedence, "REQ-014")
{
    const GearConfig config = gear_config_default();
    const float offsets[] = {-1.0f, 0.0f, 1.0f};
    const uint8_t wow[][2] = {{1, 0}, {0, 1}, {2, 0}, {0, 255}, {1, 1}};

    for (unsigned w = 0; w < sizeof wow / sizeof wow[0]; ++w) {
        for (unsigned a = 0; a < sizeof offsets / sizeof offsets[0]; ++a) {
            for (unsigned valid = 0; valid < 2; ++valid) {
                GearFixture f;

                fixture_init(&f);
                set_airborne_sample(&f, &config, config.gear_warning_altitude_ft);
                fixture_step(&f);
                f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
                fixture_step(&f);
                SP_ASSERT_EQ_INT(1, too_low_active(&f));
                SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);

                f.inputs.left_wow = wow[w][0];
                f.inputs.right_wow = wow[w][1];
                f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft + offsets[a];
                f.inputs.radio_altitude_valid = (uint8_t)valid;
                fixture_step(&f);
                SP_ASSERT_EQ_INT(0, too_low_active(&f));
                SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

                /* Even a valid high ground sample must leave no qualification memory. */
                set_airborne_sample(&f, &config, config.gear_warning_altitude_ft - 1.0f);
                fixture_step(&f);
                SP_ASSERT_EQ_INT(0, too_low_active(&f));
                SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

                f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft;
                fixture_step(&f);
                f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
                fixture_step(&f);
                SP_ASSERT_EQ_INT(1, too_low_active(&f));
                SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);
            }
        }
    }
}

SP_TEST(warning_arming_ignores_other_inputs_and_commands, "REQ-014")
{
    const GearConfig config = gear_config_default();
    const struct {
        FixtureGearPosition position;
        uint8_t handle_down;
        uint8_t alternate_extend;
        uint8_t airspeed_valid;
        uint8_t contradictory;
        float airspeed_kt;
        GearCommands commands;
    } cases[] = {{FIXTURE_GEAR_UP, 0, 0, 0, 0, 0.0f, {0, 0}},
                 {FIXTURE_GEAR_DOWN, 1, 0, 1, 0, config.gear_warning_airspeed_kt, {1, 1}},
                 {FIXTURE_GEAR_TRANSIT, 1, 1, 1, 0, config.gear_warning_airspeed_kt + 1.0f, {1, 1}},
                 {FIXTURE_GEAR_DOWN, 0, 1, 2, 0, config.gear_warning_airspeed_kt - 1.0f, {1, 0}},
                 {FIXTURE_GEAR_UP, 1, 0, 1, 0, config.min_retraction_airspeed_kt - 1.0f, {0, 1}},
                 {FIXTURE_GEAR_DOWN, 1, 0, 0, 1, config.gear_warning_airspeed_kt + 1.0f, {2, 2}}};

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        for (unsigned qualifies = 0; qualifies < 2; ++qualifies) {
            GearFixture f;
            const float altitude = config.gear_warning_altitude_ft - (qualifies ? 0.0f : 1.0f);

            fixture_init(&f);
            set_airborne_sample(&f, &config, altitude);
            fixture_set_gear(&f, cases[i].position);
            f.inputs.gear_handle_down = cases[i].handle_down;
            f.inputs.alternate_extend = cases[i].alternate_extend;
            f.inputs.airspeed_valid = cases[i].airspeed_valid;
            f.inputs.airspeed_kt = cases[i].airspeed_kt;
            if (cases[i].contradictory != 0) {
                f.inputs.nose_uplock = 1;
                f.inputs.left_uplock = 1;
                f.inputs.right_uplock = 1;
            }
            fixture_step_with(&f, &cases[i].commands);
            SP_ASSERT_EQ_INT(0, too_low_active(&f));
            SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

            /* Remove every distractor, without supplying another qualifying altitude. */
            set_airborne_sample(&f, &config, config.gear_warning_altitude_ft - 1.0f);
            fixture_step(&f);
            SP_ASSERT_EQ_INT(qualifies, too_low_active(&f));
            SP_ASSERT_EQ_INT(qualifies, f.outputs.gear_horn);
        }
    }
}

SP_TEST(warning_arming_survives_fault_and_rejected_and_accepted_reset, "REQ-014")
{
    GearFixture f;
    const GearConfig config = gear_config_default();
    const uint32_t disagree = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const GearCommands requests = {.mute = 1, .reset = 1};

    fixture_init(&f);
    set_airborne_sample(&f, &config, config.gear_warning_altitude_ft - 1.0f);
    time_out_extension(&f, &config);
    SP_ASSERT((f.status.alarms_latched & disagree) != 0);
    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

    /* Reset is rejected with transit sensors, but valid altitude must still arm. */
    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft;
    fixture_step_with(&f, &requests);
    SP_ASSERT((f.status.alarms_latched & disagree) != 0);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
    fixture_step(&f);
    SP_ASSERT_EQ_INT(1, too_low_active(&f));
    SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);

    /* An accepted fault reset must not erase the altitude qualification. */
    fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
    fixture_step_with(&f, &requests);
    SP_ASSERT_EQ_INT(0, f.status.alarms_latched & disagree);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    set_airborne_sample(&f, &config, config.gear_warning_altitude_ft - 1.0f);
    fixture_step(&f);
    SP_ASSERT_EQ_INT(1, too_low_active(&f));
    SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);
}

SP_TEST(warning_arming_ground_precedence_applies_in_fault, "REQ-014")
{
    GearFixture f;
    const GearConfig config = gear_config_default();
    const uint32_t disagree = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const GearCommands requests = {.mute = 1, .reset = 1};

    fixture_init(&f);
    set_airborne_sample(&f, &config, config.gear_warning_altitude_ft);
    fixture_step(&f);
    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
    time_out_extension(&f, &config);
    SP_ASSERT((f.status.alarms_latched & disagree) != 0);
    SP_ASSERT_EQ_INT(1, too_low_active(&f));
    SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);

    f.inputs.right_wow = 2;
    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft + 1.0f;
    fixture_step_with(&f, &requests);
    SP_ASSERT((f.status.alarms_latched & disagree) != 0);
    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

    set_airborne_sample(&f, &config, config.gear_warning_altitude_ft - 1.0f);
    fixture_step(&f);
    SP_ASSERT((f.status.alarms_latched & disagree) != 0);
    SP_ASSERT_EQ_INT(0, too_low_active(&f));
    SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft;
    fixture_step(&f);
    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft - 1.0f;
    fixture_step(&f);
    SP_ASSERT((f.status.alarms_latched & disagree) != 0);
    SP_ASSERT_EQ_INT(1, too_low_active(&f));
    SP_ASSERT_EQ_INT(1, f.outputs.gear_horn);
}

SP_TEST(warning_arming_qualification_on_accepted_fault_reset, "REQ-014")
{
    const uint32_t disagree = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint8_t validity_values[] = {0, 1, 2, UINT8_MAX};
    const GearCommands requests = {.mute = UINT8_MAX, .reset = UINT8_MAX};

    for (unsigned configured = 0; configured < 2; configured++) {
        GearConfig config = gear_config_default();

        config.gear_warning_altitude_ft += 100.0f * (float)configured;
        for (int offset = -1; offset <= 1; offset++) {
            for (size_t valid = 0; valid < sizeof validity_values / sizeof validity_values[0];
                 valid++) {
                const int qualifies = validity_values[valid] != 0 && offset >= 0;

                /* Both agreeing lever positions, with and without alternate extension. */
                for (unsigned operation = 0; operation < 4; operation++) {
                    const int down = (operation & 1U) != 0;
                    const int alternate = (operation & 2U) != 0;
                    GearFixture f;

                    fixture_init(&f);
                    SP_ASSERT_EQ_INT(SP_OK, gear_init(&f.controller, &config, NULL));
                    set_airborne_sample(&f, &config, config.gear_warning_altitude_ft - 1.0f);
                    time_out_extension(&f, &config);
                    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & disagree) != 0);
                    SP_ASSERT_EQ_INT(0, too_low_active(&f));
                    SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

                    /* Unlike retention after reset, qualification first occurs here. */
                    fixture_set_gear(&f, down ? FIXTURE_GEAR_DOWN : FIXTURE_GEAR_UP);
                    f.inputs.gear_handle_down = down ? 2 : 0;
                    f.inputs.alternate_extend = alternate ? UINT8_MAX : 0;
                    f.inputs.radio_altitude_ft = config.gear_warning_altitude_ft + (float)offset;
                    f.inputs.radio_altitude_valid = validity_values[valid];
                    fixture_step_with(&f, &requests);
                    SP_ASSERT_EQ_INT(0, f.status.alarms_latched & disagree);
                    SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
                    SP_ASSERT_EQ_INT(alternate
                                         ? GEAR_STATE_ALTERNATE_EXTENDING
                                         : (down ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_UP_LOCKED),
                                     f.status.state);
                    SP_ASSERT_EQ_INT(0, too_low_active(&f));
                    SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);

                    /* A valid low sample exposes qualification without rearming it.
                     * The reset-step mute must not pre-mute this later warning.
                     */
                    set_airborne_sample(&f, &config, config.gear_warning_altitude_ft - 1.0f);
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(qualifies, too_low_active(&f));
                    SP_ASSERT_EQ_INT(qualifies, f.outputs.gear_horn);
                }
            }
        }
    }
}

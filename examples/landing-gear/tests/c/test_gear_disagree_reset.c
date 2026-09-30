#include "gear_fixture.h"
#include "sp_test.h"

enum {
    DISAGREE_DOWN_LOCK_MASK = 0x07,
    DISAGREE_UP_LOCK_MASK = 0x38,
    DISAGREE_LOCK_COMBINATIONS = 64
};

static void disagree_step(GearFixture *f, const GearCommands *commands, uint32_t dt_ms)
{
    gear_step(&f->controller, &f->inputs, commands, dt_ms, &f->outputs);
    gear_get_status(&f->controller, &f->status);
}

/* Bits 0..2 are nose/left/right downlocks; bits 3..5 are the uplocks. */
static void disagree_set_lock_mask(GearInputs *inputs, unsigned mask, uint8_t asserted)
{
    inputs->nose_downlock = (mask & 0x01U) != 0 ? asserted : 0;
    inputs->left_downlock = (mask & 0x02U) != 0 ? asserted : 0;
    inputs->right_downlock = (mask & 0x04U) != 0 ? asserted : 0;
    inputs->nose_uplock = (mask & 0x08U) != 0 ? asserted : 0;
    inputs->left_uplock = (mask & 0x10U) != 0 ? asserted : 0;
    inputs->right_uplock = (mask & 0x20U) != 0 ? asserted : 0;
}

static void disagree_start_transit(GearFixture *f, uint8_t handle_down, int faulted)
{
    const GearConfig config = gear_config_default();

    fixture_init(f);
    fixture_fly(f, 3000.0f, config.min_retraction_airspeed_kt);
    fixture_set_gear(f, FIXTURE_GEAR_TRANSIT);
    f->inputs.gear_handle_down = handle_down;
    disagree_step(f, NULL, 1);
    if (faulted) {
        /* Raise the latch through a failed transit, never by editing controller storage. */
        disagree_step(f, NULL, config.normal_transit_timeout_ms);
    }
}

SP_TEST(disagree_reset_requires_requested_lever_lock_agreement, "REQ-008")
{
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint8_t lever_values[] = {0, 1, 2, UINT8_MAX};
    const uint8_t asserted_values[] = {1, 2, UINT8_MAX};
    const uint8_t reset_values[] = {0, 1, 2, UINT8_MAX};
    const size_t reset_count = sizeof reset_values / sizeof reset_values[0];

    for (size_t lever = 0; lever < sizeof lever_values / sizeof lever_values[0]; lever++) {
        const int handle_down = lever_values[lever] != 0;
        const unsigned complete_mask =
            handle_down ? DISAGREE_DOWN_LOCK_MASK : DISAGREE_UP_LOCK_MASK;
        const GearState locked_state = handle_down ? GEAR_STATE_DOWN_LOCKED : GEAR_STATE_UP_LOCKED;

        for (size_t value = 0; value < sizeof asserted_values / sizeof asserted_values[0];
             value++) {
            for (unsigned mask = 0; mask < DISAGREE_LOCK_COMBINATIONS; mask++) {
                /* The final case supplies NULL instead of a zero-valued command structure. */
                for (size_t request = 0; request <= reset_count; request++) {
                    GearFixture f;
                    const GearCommands commands = {
                        .reset = request < reset_count ? reset_values[request] : 0};
                    const GearCommands *command_ptr = request < reset_count ? &commands : NULL;
                    const int agreement = mask == complete_mask;
                    const int accepted = agreement && commands.reset != 0;

                    disagree_start_transit(&f, lever_values[lever], 1);
                    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
                    SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

                    /* Covers two locks, exactly three, and three plus a contrary lock,
                     * as well as fully locked gear opposite to the selected lever.
                     */
                    disagree_set_lock_mask(&f.inputs, mask, asserted_values[value]);
                    disagree_step(&f, command_ptr, 1);
                    SP_ASSERT_EQ_INT(accepted ? locked_state : GEAR_STATE_FAULT, f.status.state);
                    SP_ASSERT_EQ_INT(!agreement, (f.status.alarms_active & fault) != 0);
                    SP_ASSERT_EQ_INT(!accepted, (f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(!accepted, f.outputs.master_warning);
                    if (accepted) {
                        SP_ASSERT_EQ_INT(0, (f.status.alarms_unacked & fault) != 0);
                    }
                }
            }
        }
    }
}

SP_TEST(disagree_master_warning_is_independent_of_horn_mute, "REQ-008,REQ-011")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const uint8_t mute_values[] = {1, 2, UINT8_MAX};

    SP_ASSERT(config.normal_transit_timeout_ms > 3U);
    SP_ASSERT(config.horn_mute_min_altitude_ft > 1.0f);
    SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

    for (unsigned faulted = 0; faulted < 2; faulted++) {
        for (int offset = -1; offset <= 1; offset++) {
            for (size_t mute = 0; mute < sizeof mute_values / sizeof mute_values[0]; mute++) {
                GearFixture f;
                const GearCommands commands = {.mute = mute_values[mute]};
                const GearState expected = faulted != 0 ? GEAR_STATE_FAULT : GEAR_STATE_EXTENDING;

                disagree_start_transit(&f, 1, (int)faulted);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, f.outputs.master_warning);

                /* A real horn warning makes the above-limit mute demonstrably effective.
                 * At and below the limit it is rejected, with the same master-warning result.
                 */
                fixture_fly(&f, config.horn_mute_min_altitude_ft + (float)offset,
                            config.gear_warning_airspeed_kt - 1.0f);
                disagree_step(&f, &commands, 1);
                SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);
                SP_ASSERT_EQ_INT(expected, f.status.state);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, f.outputs.master_warning);

                /* Neither a persistent horn mute nor absence of a further request resets it. */
                disagree_step(&f, NULL, 1);
                SP_ASSERT_EQ_INT(offset <= 0, f.outputs.gear_horn);
                SP_ASSERT_EQ_INT(expected, f.status.state);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, f.outputs.master_warning);

                /* Even with resolved disagreement, mute must not substitute for reset. */
                fixture_set_gear(&f, FIXTURE_GEAR_DOWN);
                disagree_step(&f, &commands, 1);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
                SP_ASSERT_EQ_INT(0, (f.status.alarms_active & warning) != 0);
                SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);
                SP_ASSERT_EQ_INT(faulted, (f.status.alarms_latched & fault) != 0);
                SP_ASSERT_EQ_INT(faulted, f.outputs.master_warning);
                SP_ASSERT_EQ_INT(faulted != 0 ? GEAR_STATE_FAULT : GEAR_STATE_DOWN_LOCKED,
                                 f.status.state);
            }
        }
    }
}

SP_TEST(disagree_reset_preserves_horn_warning_and_rearms_normal_transit, "REQ-008,REQ-007")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const uint32_t warning = GEAR_FAULT_BIT(GEAR_FAULT_TOO_LOW_GEAR);
    const GearCommands reset = {.reset = 1};

    SP_ASSERT(config.normal_transit_timeout_ms > 1U);
    SP_ASSERT(config.horn_mute_min_altitude_ft + 1.0f < config.gear_warning_altitude_ft);
    SP_ASSERT(config.gear_warning_airspeed_kt > 1.0f);

    for (unsigned muted = 0; muted < 2; muted++) {
        GearFixture f;
        const GearCommands mute = {.mute = (uint8_t)muted};
        uint32_t warning_unacked;

        disagree_start_transit(&f, 0, 1);
        SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

        /* Up-and-locked agreement still permits an independent too-low gear warning. */
        fixture_set_gear(&f, FIXTURE_GEAR_UP);
        fixture_fly(&f, config.horn_mute_min_altitude_ft + 1.0f,
                    config.gear_warning_airspeed_kt - 1.0f);
        disagree_step(&f, &mute, 1);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(!muted, f.outputs.gear_horn);
        SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
        warning_unacked = f.status.alarms_unacked & warning;

        disagree_step(&f, &reset, 1);
        SP_ASSERT_EQ_INT(GEAR_STATE_UP_LOCKED, f.status.state);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_unacked & fault) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
        SP_ASSERT_EQ_INT(1, (f.status.alarms_active & warning) != 0);
        SP_ASSERT_EQ_INT(warning_unacked, f.status.alarms_unacked & warning);
        SP_ASSERT_EQ_INT(!muted, f.outputs.gear_horn);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.up_valve);

        /* Loss of completion after reset must resume normal drive without the old timeout. */
        fixture_fly(&f, 3000.0f, config.min_retraction_airspeed_kt);
        fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
        disagree_step(&f, NULL, 1);
        SP_ASSERT_EQ_INT(GEAR_STATE_RETRACTING, f.status.state);
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        SP_ASSERT_EQ_INT(0, f.outputs.gear_horn);
        SP_ASSERT_EQ_INT(0, f.status.alarms_active);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.master_warning);

        /* Below, at, and above the fresh post-reset timeout, not the original transit. */
        disagree_step(&f, NULL, config.normal_transit_timeout_ms - 1U);
        SP_ASSERT_EQ_INT(GEAR_STATE_RETRACTING, f.status.state);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_active & fault) != 0);
        SP_ASSERT_EQ_INT(0, (f.status.alarms_latched & fault) != 0);
        SP_ASSERT_EQ_INT(0, f.outputs.master_warning);
        SP_ASSERT_EQ_INT(1, f.outputs.up_valve);

        for (unsigned boundary = 0; boundary < 2; boundary++) {
            disagree_step(&f, NULL, 1);
            SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_active & fault) != 0);
            SP_ASSERT_EQ_INT(1, (f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
            SP_ASSERT_EQ_INT(1, f.outputs.up_valve);
            SP_ASSERT_EQ_INT(0, f.outputs.down_valve);
        }
    }
}

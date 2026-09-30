#include "gear_fixture.h"
#include "sp_test.h"

/* Zero, first nonzero value, its successor, and the largest input value. */
static const uint8_t discrete_values[] = {0, 1, 2, UINT8_MAX};

#define DISCRETE_VALUE_COUNT (sizeof discrete_values / sizeof discrete_values[0])

SP_TEST(handle_lock_truth_table_and_transitions, "REQ-002")
{
    for (unsigned lever = 0; lever < DISCRETE_VALUE_COUNT; ++lever) {
        for (unsigned alternate = 0; alternate < DISCRETE_VALUE_COUNT; ++alternate) {
            for (unsigned left = 0; left < DISCRETE_VALUE_COUNT; ++left) {
                for (unsigned right = 0; right < DISCRETE_VALUE_COUNT; ++right) {
                    GearFixture f;
                    const int expected = discrete_values[left] != 0 || discrete_values[right] != 0;

                    fixture_init(&f);
                    f.inputs.gear_handle_down = discrete_values[lever];
                    f.inputs.alternate_extend = discrete_values[alternate];
                    f.inputs.left_wow = discrete_values[left];
                    f.inputs.right_wow = discrete_values[right];

                    /* Evaluate current inputs on the first step and retain the command. */
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(expected, f.outputs.handle_lock);
                    SP_ASSERT(!fixture_raised(&f, GEAR_FAULT_GEAR_DISAGREE));
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(expected, f.outputs.handle_lock);

                    /* Clearing both switches must release the lock in the same step. */
                    fixture_set_ground(&f, 0);
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(0, f.outputs.handle_lock);

                    /* Reasserting either switch must relock without a delay. */
                    f.inputs.left_wow = discrete_values[left];
                    f.inputs.right_wow = discrete_values[right];
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(expected, f.outputs.handle_lock);
                }
            }
        }
    }
}

SP_TEST(handle_lock_independent_of_latched_fault, "REQ-002")
{
    const GearConfig config = gear_config_default();
    const uint32_t fault = GEAR_FAULT_BIT(GEAR_FAULT_GEAR_DISAGREE);
    const GearCommands none = {0};

    SP_ASSERT(config.normal_transit_timeout_ms > 0);
    for (unsigned lever = 0; lever < DISCRETE_VALUE_COUNT; ++lever) {
        for (unsigned alternate = 0; alternate < DISCRETE_VALUE_COUNT; ++alternate) {
            GearFixture f;

            fixture_init(&f);
            fixture_set_gear(&f, FIXTURE_GEAR_TRANSIT);
            fixture_fly(&f, 3000.0f, 220.0f);
            fixture_step(&f);
            SP_ASSERT(!fixture_raised(&f, GEAR_FAULT_GEAR_DISAGREE));
            SP_ASSERT_EQ_INT(1, f.outputs.down_valve);

            /* Leave the transit incomplete until a real fault is latched. */
            gear_step(&f.controller, &f.inputs, &none, config.normal_transit_timeout_ms,
                      &f.outputs);
            gear_get_status(&f.controller, &f.status);
            SP_ASSERT((f.status.alarms_latched & fault) != 0);
            SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
            SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
            SP_ASSERT_EQ_INT(0, f.outputs.handle_lock);

            f.inputs.gear_handle_down = discrete_values[lever];
            f.inputs.alternate_extend = discrete_values[alternate];
            for (unsigned left = 0; left < DISCRETE_VALUE_COUNT; ++left) {
                for (unsigned right = 0; right < DISCRETE_VALUE_COUNT; ++right) {
                    const int expected = discrete_values[left] != 0 || discrete_values[right] != 0;

                    f.inputs.left_wow = discrete_values[left];
                    f.inputs.right_wow = discrete_values[right];
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(expected, f.outputs.handle_lock);
                    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
                    SP_ASSERT((f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(1, f.outputs.master_warning);

                    /* A fault must not hold the lever lock after both WOW inputs clear. */
                    fixture_set_ground(&f, 0);
                    fixture_step(&f);
                    SP_ASSERT_EQ_INT(0, f.outputs.handle_lock);
                    SP_ASSERT_EQ_INT(GEAR_STATE_FAULT, f.status.state);
                    SP_ASSERT((f.status.alarms_latched & fault) != 0);
                    SP_ASSERT_EQ_INT(1, f.outputs.master_warning);
                }
            }
        }
    }
}

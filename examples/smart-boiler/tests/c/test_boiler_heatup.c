#ifndef SP_TEST_SUITE
#define SP_TEST_SUITE "boiler_heatup"
#endif

#include "sp_test.h"

#include "boiler_fixture.h"

#define HEATUP_TIMEOUT_MS 1200000U
#define HEATUP_BIT        BOILER_FAULT_BIT(BOILER_FAULT_HEATUP_TIMEOUT)

static void heatup_step(BoilerFixture *f, const BoilerCommands *commands, uint32_t dt_ms)
{
    boiler_step(&f->controller, &f->inputs, commands, dt_ms, &f->outputs);
    boiler_get_status(&f->controller, &f->status);
}

/* Follow the plant between samples, including a final step shorter than STEP_MS. */
static void heatup_advance(BoilerFixture *f, uint32_t duration_ms)
{
    const BoilerCommands none = {0};

    while (duration_ms != 0) {
        const uint32_t dt_ms = duration_ms < STEP_MS ? duration_ms : STEP_MS;

        heatup_step(f, &none, dt_ms);
        fixture_follow_outputs(f);
        duration_ms -= dt_ms;
    }
}

static SpStatus heatup_start(BoilerFixture *f, uint32_t timeout_ms)
{
    BoilerConfig config = boiler_config_default();
    const BoilerCommands start = {.start = 1};
    SpStatus result;

    fixture_init(f);
    config.heatup_timeout_ms = timeout_ms;
    result = boiler_init(&f->controller, &config, NULL);
    if (result != SP_OK) {
        return result;
    }
    fixture_reach_standby(f);
    fixture_command(f, start);
    fixture_follow_outputs(f);
    return SP_OK;
}

static bool heatup_warning(const BoilerFixture *f)
{
    return (f->status.alarms_active & HEATUP_BIT) != 0;
}

static bool same_outputs(const BoilerOutputs *a, const BoilerOutputs *b)
{
    return a->pump_run == b->pump_run && a->valve_open == b->valve_open &&
           a->heater_contactor == b->heater_contactor &&
           a->heater_power_pct == b->heater_power_pct && a->alarm_horn == b->alarm_horn;
}

SP_TEST(heatup_timeout_boundaries_and_unaffected_outputs, "REQ-042")
{
    BoilerFixture f;
    const BoilerConfig defaults = boiler_config_default();
    const BoilerCommands start_and_ack = {.start = 1, .ack = 1};
    const BoilerCommands none = {0};
    BoilerOutputs before;
    uint32_t accepted_at;

    SP_ASSERT_EQ_INT(HEATUP_TIMEOUT_MS, defaults.heatup_timeout_ms);
    SP_ASSERT_EQ_INT(SP_OK, heatup_start(&f, HEATUP_TIMEOUT_MS));
    SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
    SP_ASSERT(!heatup_warning(&f));
    accepted_at = f.status.uptime_ms;

    /* This interval includes all time spent in STARTUP after acceptance. */
    heatup_advance(&f, HEATUP_TIMEOUT_MS - 1U);
    SP_ASSERT_EQ_INT(HEATUP_TIMEOUT_MS - 1U, f.status.uptime_ms - accepted_at);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT_EQ_INT(0, f.status.alarms_active);
    SP_ASSERT_EQ_INT(1, f.outputs.heater_contactor);
    SP_ASSERT(f.outputs.heater_power_pct > 0.0f);
    before = f.outputs;

    heatup_step(&f, &none, 1U);
    SP_ASSERT(!heatup_warning(&f));
    SP_ASSERT(same_outputs(&before, &f.outputs));

    heatup_step(&f, &none, 1U);
    SP_ASSERT(heatup_warning(&f));
    SP_ASSERT_EQ_INT(HEATUP_BIT, f.status.alarms_active);
    SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);
    SP_ASSERT(f.status.alarms_unacked & HEATUP_BIT);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT(same_outputs(&before, &f.outputs));
    SP_ASSERT_EQ_INT(0, f.outputs.alarm_horn);

    /* Neither acknowledgement nor an ignored RUNNING start resets monitoring. */
    heatup_step(&f, &start_and_ack, 1U);
    SP_ASSERT(heatup_warning(&f));
    SP_ASSERT_EQ_INT(0, f.status.alarms_unacked & HEATUP_BIT);
    SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT(same_outputs(&before, &f.outputs));
}

SP_TEST(heatup_accepting_step_excludes_pretransition_time, "REQ-042")
{
    BoilerFixture f;
    const BoilerCommands start = {.start = 1};
    const BoilerCommands none = {0};

    fixture_init(&f);
    fixture_reach_standby(&f);

    /* Even a long accepting step contributes no time preceding the transition. */
    heatup_step(&f, &start, HEATUP_TIMEOUT_MS + 1U);
    fixture_follow_outputs(&f);
    SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
    SP_ASSERT(!heatup_warning(&f));

    heatup_advance(&f, HEATUP_TIMEOUT_MS);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT(!heatup_warning(&f));
    heatup_step(&f, &none, 1U);
    SP_ASSERT(heatup_warning(&f));
}

SP_TEST(heatup_can_timeout_in_startup, "REQ-042")
{
    BoilerFixture f;
    const BoilerCommands none = {0};
    const uint32_t timeout_ms = 1000U;
    BoilerOutputs before;

    /* A shorter configured timeout makes a slow valve observable before its trip. */
    SP_ASSERT_EQ_INT(SP_OK, heatup_start(&f, timeout_ms));
    SP_ASSERT(f.controller.config.valve_travel_timeout_ms > timeout_ms + 1U);
    fixture_set_valve_position(&f, 0.0f);

    heatup_step(&f, &none, timeout_ms - 1U);
    SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
    SP_ASSERT(!heatup_warning(&f));
    heatup_step(&f, &none, 1U);
    SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
    SP_ASSERT(!heatup_warning(&f));
    before = f.outputs;
    heatup_step(&f, &none, 1U);
    SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
    SP_ASSERT_EQ_INT(HEATUP_BIT, f.status.alarms_active);
    SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);
    SP_ASSERT(same_outputs(&before, &f.outputs));
}

SP_TEST(heatup_completion_boundaries_take_precedence_and_do_not_rearm, "REQ-042")
{
    const float offsets[] = {-0.1f, 0.0f, 0.1f};
    const BoilerCommands none = {0};
    const BoilerCommands start = {.start = 1};

    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        BoilerFixture f;
        float target;

        SP_ASSERT_EQ_INT(SP_OK, heatup_start(&f, HEATUP_TIMEOUT_MS));
        heatup_advance(&f, HEATUP_TIMEOUT_MS);
        SP_ASSERT(!heatup_warning(&f));
        target = f.status.setpoint_c;
        fixture_set_temperature(&f, target + offsets[i]);

        /* Completion and the first overdue instant occur in the same step. */
        heatup_step(&f, &none, 1U);
        SP_ASSERT_EQ_INT(1, f.status.temperature_valid);
        SP_ASSERT_NEAR(target + offsets[i], f.status.temperature_c, 0.001f);
        SP_ASSERT_EQ_INT(offsets[i] < 0.0f, heatup_warning(&f));
        SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
        SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);

        if (offsets[i] >= 0.0f) {
            /* Cooling and another RUNNING start must not rearm completed monitoring. */
            fixture_set_temperature(&f, 20.0f);
            heatup_step(&f, &start, 1U);
            fixture_follow_outputs(&f);
            heatup_advance(&f, HEATUP_TIMEOUT_MS + 1U);
            SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
            SP_ASSERT(!heatup_warning(&f));
        }
    }
}

SP_TEST(heatup_active_warning_clears_at_completion_boundary, "REQ-042")
{
    const float offsets[] = {-0.1f, 0.0f, 0.1f};
    const BoilerCommands none = {0};

    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        BoilerFixture f;
        float target;

        SP_ASSERT_EQ_INT(SP_OK, heatup_start(&f, HEATUP_TIMEOUT_MS));
        heatup_advance(&f, HEATUP_TIMEOUT_MS + 1U);
        SP_ASSERT(heatup_warning(&f));
        target = f.status.setpoint_c;
        fixture_set_temperature(&f, target + offsets[i]);
        heatup_step(&f, &none, 1U);
        SP_ASSERT_EQ_INT(1, f.status.temperature_valid);
        SP_ASSERT_NEAR(target + offsets[i], f.status.temperature_c, 0.001f);
        SP_ASSERT_EQ_INT(offsets[i] < 0.0f, heatup_warning(&f));
        SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);
        SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    }
}

SP_TEST(heatup_invalid_temperature_neither_completes_nor_pauses_time, "REQ-042")
{
    BoilerFixture f;
    const BoilerCommands none = {0};

    SP_ASSERT_EQ_INT(SP_OK, heatup_start(&f, HEATUP_TIMEOUT_MS));
    heatup_advance(&f, HEATUP_TIMEOUT_MS - 1U);
    SP_ASSERT(!heatup_warning(&f));

    /* An overrange current is unusable, even though it represents a high temperature. */
    f.inputs.temperature_ma = f.controller.config.loop_fault_high_ma + 1.0f;
    heatup_step(&f, &none, 1U);
    SP_ASSERT_EQ_INT(0, f.status.temperature_valid);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT(!heatup_warning(&f));
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
    SP_ASSERT_NEAR(0.0f, f.outputs.heater_power_pct, 0.001f);

    heatup_step(&f, &none, 1U);
    SP_ASSERT_EQ_INT(0, f.status.temperature_valid);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT_EQ_INT(HEATUP_BIT, f.status.alarms_active);
    SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);

    fixture_set_temperature(&f, f.status.setpoint_c);
    heatup_step(&f, &none, 1U);
    SP_ASSERT_EQ_INT(1, f.status.temperature_valid);
    SP_ASSERT(!heatup_warning(&f));
}

SP_TEST(heatup_already_hot_start_completes_in_accepting_step, "REQ-042")
{
    BoilerFixture f;
    const BoilerCommands start = {.start = 1};

    fixture_init(&f);
    fixture_reach_standby(&f);
    fixture_set_temperature(&f, f.status.setpoint_c);
    heatup_step(&f, &start, STEP_MS);
    fixture_follow_outputs(&f);
    SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
    SP_ASSERT_EQ_INT(1, f.status.temperature_valid);
    SP_ASSERT(!heatup_warning(&f));

    fixture_set_temperature(&f, 20.0f);
    heatup_advance(&f, HEATUP_TIMEOUT_MS + 1U);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT(!heatup_warning(&f));
}

SP_TEST(heatup_stop_cancels_and_new_standby_start_gets_a_fresh_timer, "REQ-042")
{
    const BoilerCommands stop = {.stop = 1};
    const BoilerCommands start = {.start = 1};
    const BoilerCommands none = {0};

    for (unsigned stop_in_running = 0; stop_in_running < 2; ++stop_in_running) {
        BoilerFixture f;

        SP_ASSERT_EQ_INT(SP_OK, heatup_start(&f, HEATUP_TIMEOUT_MS));
        if (stop_in_running) {
            heatup_advance(&f, HEATUP_TIMEOUT_MS + 1U);
            SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
            SP_ASSERT(heatup_warning(&f));
        } else {
            SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
        }

        heatup_step(&f, &stop, 1U);
        fixture_follow_outputs(&f);
        SP_ASSERT(f.status.state != BOILER_STATE_STARTUP);
        SP_ASSERT(f.status.state != BOILER_STATE_RUNNING);
        SP_ASSERT(!heatup_warning(&f));
        SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);

        /* Cancelled monitoring remains inactive throughout an idle timeout interval. */
        heatup_advance(&f, HEATUP_TIMEOUT_MS + 1U);
        SP_ASSERT_EQ_INT(BOILER_STATE_STANDBY, f.status.state);
        SP_ASSERT(!heatup_warning(&f));

        heatup_step(&f, &start, 1U);
        fixture_follow_outputs(&f);
        SP_ASSERT_EQ_INT(BOILER_STATE_STARTUP, f.status.state);
        SP_ASSERT(!heatup_warning(&f));
        heatup_advance(&f, HEATUP_TIMEOUT_MS - 1U);
        SP_ASSERT(!heatup_warning(&f));
        heatup_step(&f, &none, 1U);
        SP_ASSERT(!heatup_warning(&f));
        heatup_step(&f, &none, 1U);
        SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
        SP_ASSERT(heatup_warning(&f));
    }
}

SP_TEST(heatup_critical_fault_cancels_warning_and_ignored_start_cannot_rearm, "REQ-042")
{
    BoilerFixture f;
    const BoilerCommands none = {0};
    const BoilerCommands start = {.start = 1};

    SP_ASSERT_EQ_INT(SP_OK, heatup_start(&f, HEATUP_TIMEOUT_MS));
    heatup_advance(&f, HEATUP_TIMEOUT_MS + 1U);
    SP_ASSERT(heatup_warning(&f));

    fixture_set_temperature(&f, f.controller.config.temperature_trip_c + 0.1f);
    heatup_step(&f, &none, 1U);
    fixture_follow_outputs(&f);
    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
    SP_ASSERT(!heatup_warning(&f));
    SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);
    SP_ASSERT(f.status.alarms_latched != 0);
    SP_ASSERT_EQ_INT(0, f.outputs.heater_contactor);
    SP_ASSERT_NEAR(0.0f, f.outputs.heater_power_pct, 0.001f);

    fixture_set_temperature(&f, 20.0f);
    heatup_step(&f, &start, 1U);
    fixture_follow_outputs(&f);
    heatup_advance(&f, HEATUP_TIMEOUT_MS + 1U);
    SP_ASSERT_EQ_INT(BOILER_STATE_FAULT, f.status.state);
    SP_ASSERT(!heatup_warning(&f));
}

SP_TEST(heatup_raised_setpoint_changes_target_without_restarting_timer, "REQ-042")
{
    BoilerFixture f;
    const BoilerCommands none = {0};
    BoilerCommands change = {.set_setpoint = 1};
    float original_target;

    SP_ASSERT_EQ_INT(SP_OK, heatup_start(&f, HEATUP_TIMEOUT_MS));
    original_target = f.status.setpoint_c;
    change.setpoint_c = original_target + 1.0f;
    SP_ASSERT(change.setpoint_c <= f.controller.config.setpoint_max_c);
    heatup_advance(&f, HEATUP_TIMEOUT_MS - 2U);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);

    /* This reading would complete heat-up against the old target. */
    fixture_set_temperature(&f, original_target);
    heatup_step(&f, &change, 1U);
    SP_ASSERT_EQ_INT(1, f.status.temperature_valid);
    SP_ASSERT_NEAR(original_target, f.status.temperature_c, 0.001f);
    SP_ASSERT_NEAR(change.setpoint_c, f.status.setpoint_c, 0.001f);
    SP_ASSERT(!heatup_warning(&f));

    /* The original deadline still applies: below, exactly at, and above it. */
    heatup_step(&f, &none, 1U);
    SP_ASSERT(!heatup_warning(&f));
    heatup_step(&f, &none, 1U);
    SP_ASSERT_EQ_INT(HEATUP_BIT, f.status.alarms_active);
    SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);
    SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
    SP_ASSERT_EQ_INT(0, f.outputs.alarm_horn);
}

SP_TEST(heatup_lowered_setpoint_completion_boundaries_and_no_rearm, "REQ-042")
{
    const float offsets[] = {-0.1f, 0.0f, 0.1f};

    for (unsigned already_overdue = 0; already_overdue < 2; ++already_overdue) {
        for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
            BoilerFixture f;
            BoilerCommands change = {.set_setpoint = 1};
            float original_target;
            float lowered_target;

            SP_ASSERT_EQ_INT(SP_OK, heatup_start(&f, HEATUP_TIMEOUT_MS));
            original_target = f.status.setpoint_c;
            lowered_target = original_target - 1.0f;
            SP_ASSERT(lowered_target >= f.controller.config.setpoint_min_c);
            heatup_advance(&f, HEATUP_TIMEOUT_MS + already_overdue);
            SP_ASSERT_EQ_INT(already_overdue != 0, heatup_warning(&f));

            /* Lower the target on the first overdue step or with a standing warning. */
            fixture_set_temperature(&f, lowered_target + offsets[i]);
            change.setpoint_c = lowered_target;
            heatup_step(&f, &change, 1U);
            SP_ASSERT_EQ_INT(1, f.status.temperature_valid);
            SP_ASSERT_NEAR(lowered_target + offsets[i], f.status.temperature_c, 0.001f);
            SP_ASSERT_NEAR(lowered_target, f.status.setpoint_c, 0.001f);
            SP_ASSERT_EQ_INT(offsets[i] < 0.0f, heatup_warning(&f));
            SP_ASSERT_EQ_INT(0, f.status.alarms_latched & HEATUP_BIT);
            SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
            SP_ASSERT_EQ_INT(0, f.outputs.alarm_horn);

            if (offsets[i] >= 0.0f) {
                /* Raising the target after completion must not begin another heat-up. */
                fixture_set_temperature(&f, 20.0f);
                change.setpoint_c = original_target;
                heatup_step(&f, &change, 1U);
                fixture_follow_outputs(&f);
                SP_ASSERT_NEAR(original_target, f.status.setpoint_c, 0.001f);
                SP_ASSERT(!heatup_warning(&f));
                heatup_advance(&f, HEATUP_TIMEOUT_MS + 1U);
                SP_ASSERT_EQ_INT(BOILER_STATE_RUNNING, f.status.state);
                SP_ASSERT(!heatup_warning(&f));
            }
        }
    }
}

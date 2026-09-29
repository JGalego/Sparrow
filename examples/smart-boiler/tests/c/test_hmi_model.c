#include <math.h>
#include <string.h>

#include "boiler_actions.h"
#include "boiler_app.h"
#include "boiler_view.h"
#include "sp_test.h"

static BoilerApp app;

static BoilerStatus healthy_status(BoilerState state, uint32_t uptime_ms)
{
    BoilerStatus status = {0};

    status.state = (uint8_t)state;
    status.uptime_ms = uptime_ms;
    status.temperature_c = 80.0f;
    status.pressure_bar = 2.4f;
    status.flow_lpm = 40.0f;
    status.valve_position_pct = 100.0f;
    status.setpoint_c = 80.0f;
    status.temperature_valid = status.pressure_valid = status.flow_valid = 1;
    status.valve_valid = 1;
    return status;
}

static BoilerViewModel view_of(const BoilerApp *state)
{
    BoilerViewModel view;

    boiler_view_build(&view, state);
    return view;
}

static const BoilerNotification *newest(void)
{
    return boiler_notify_get(&app.notifications, 0);
}

/* REQ-030: severity */

SP_TEST(healthy_values_are_normal_and_formatted, "REQ-030")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 1000);
    boiler_app_ingest(&app, &status, 0);

    const BoilerViewModel view = view_of(&app);

    SP_ASSERT(strcmp(view.temperature.text, "80.0") == 0);
    SP_ASSERT(strcmp(view.pressure.text, "2.40") == 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_NORMAL, view.temperature.severity);
    SP_ASSERT_EQ_INT(SP_SEVERITY_NORMAL, view.pressure.severity);
}

SP_TEST(temperature_warning_and_trip_map_to_severity, "REQ-030")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 1000);

    status.alarms_active = BOILER_FAULT_BIT(BOILER_FAULT_TEMP_HIGH);
    boiler_app_ingest(&app, &status, 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_WARNING, view_of(&app).temperature.severity);

    status.alarms_active = BOILER_FAULT_BIT(BOILER_FAULT_OVER_TEMP);
    boiler_app_ingest(&app, &status, 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_CRITICAL, view_of(&app).temperature.severity);
}

SP_TEST(latched_alarm_keeps_the_value_critical, "REQ-030")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_FAULT, 1000);
    status.alarms_latched = BOILER_FAULT_BIT(BOILER_FAULT_OVER_PRESSURE);

    boiler_app_ingest(&app, &status, 0);

    SP_ASSERT_EQ_INT(SP_SEVERITY_CRITICAL, view_of(&app).pressure.severity);
}

SP_TEST(low_pressure_is_critical_and_high_pressure_a_warning, "REQ-030")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 1000);

    status.alarms_active = BOILER_FAULT_BIT(BOILER_FAULT_LOW_PRESSURE);
    boiler_app_ingest(&app, &status, 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_CRITICAL, view_of(&app).pressure.severity);

    status.alarms_active = BOILER_FAULT_BIT(BOILER_FAULT_PRESSURE_HIGH);
    boiler_app_ingest(&app, &status, 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_WARNING, view_of(&app).pressure.severity);
}

SP_TEST(invalid_reading_is_shown_as_dashes, "REQ-030")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 1000);
    status.temperature_valid = 0;
    status.temperature_c = 0.0f;

    boiler_app_ingest(&app, &status, 0);

    SP_ASSERT(strcmp(view_of(&app).temperature.text, "--") == 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_INACTIVE, view_of(&app).temperature.severity);
}

SP_TEST(state_severity_follows_the_state, "REQ-030")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 1000);
    boiler_app_ingest(&app, &status, 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_NORMAL, view_of(&app).state_severity);

    status.state = BOILER_STATE_FAULT;
    boiler_app_ingest(&app, &status, 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_CRITICAL, view_of(&app).state_severity);
    SP_ASSERT(strcmp(view_of(&app).state_text, "Fault") == 0);
}

SP_TEST(headline_names_the_most_urgent_alarm, "REQ-030")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_FAULT, 1000);
    status.alarms_active = BOILER_FAULT_BIT(BOILER_FAULT_TEMP_HIGH);
    status.alarms_latched = BOILER_FAULT_BIT(BOILER_FAULT_OVER_TEMP);

    boiler_app_ingest(&app, &status, 0);
    const BoilerViewModel view = view_of(&app);

    SP_ASSERT_EQ_INT(2, view.alarm_count);
    SP_ASSERT_EQ_INT(SP_SEVERITY_CRITICAL, view.alarm_severity);
    SP_ASSERT(strcmp(view.alarm_headline, "Over-temperature trip") == 0);
}

SP_TEST(uptime_is_formatted_as_clock_time, "REQ-030")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 3723000);

    boiler_app_ingest(&app, &status, 0);

    SP_ASSERT(strcmp(view_of(&app).uptime_text, "01:02:03") == 0);
}

/* REQ-031: history */

SP_TEST(history_is_sampled_once_per_second, "REQ-031")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 0);

    for (uint32_t ms = 0; ms < 5000; ms += 100) {
        status.uptime_ms = ms;
        boiler_app_ingest(&app, &status, ms);
    }

    SP_ASSERT_EQ_INT(5, sp_ring_count(&app.temperature_history));
}

SP_TEST(history_records_a_gap_for_an_invalid_reading, "REQ-031")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 0);
    status.temperature_valid = 0;
    float value;

    boiler_app_ingest(&app, &status, 0);

    SP_ASSERT(sp_ring_get(&app.temperature_history, 0, &value));
    SP_ASSERT(isnan(value));
}

SP_TEST(history_keeps_one_hour_and_drops_older_samples, "REQ-031")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 0);

    for (uint32_t second = 0; second < BOILER_HISTORY_SECONDS + 100; second++) {
        status.uptime_ms = second * 1000;
        status.temperature_c = (float)second;
        boiler_app_ingest(&app, &status, second * 1000);
    }

    SP_ASSERT_EQ_INT(BOILER_HISTORY_SECONDS, sp_ring_count(&app.temperature_history));
    float oldest;
    SP_ASSERT(sp_ring_get(&app.temperature_history, BOILER_HISTORY_SECONDS - 1, &oldest));
    SP_ASSERT_NEAR(100.0, oldest, 0);
}

SP_TEST(chart_series_covers_the_selected_range, "REQ-031")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 0);
    float points[BOILER_CHART_POINTS];

    for (uint32_t second = 0; second < 600; second++) {
        status.uptime_ms = second * 1000;
        status.temperature_c = (float)second;
        boiler_app_ingest(&app, &status, second * 1000);
    }

    SP_ASSERT_EQ_INT(BOILER_CHART_POINTS,
                     boiler_app_series(&app, BOILER_SERIES_TEMPERATURE, BOILER_CHART_10_MIN, points,
                                       BOILER_CHART_POINTS));
    SP_ASSERT_NEAR(2.0, points[0], 1e-3);
    SP_ASSERT_NEAR(597.0, points[BOILER_CHART_POINTS - 1], 1e-3);
}

SP_TEST(chart_series_leaves_unrecorded_time_empty, "REQ-031")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 0);
    float points[BOILER_CHART_POINTS];
    boiler_app_ingest(&app, &status, 0);

    boiler_app_series(&app, BOILER_SERIES_PRESSURE, BOILER_CHART_60_MIN, points,
                      BOILER_CHART_POINTS);

    SP_ASSERT(isnan(points[0]));
    SP_ASSERT_NEAR(2.4, points[BOILER_CHART_POINTS - 1], 1e-3);
}

/* REQ-032: notifications */

SP_TEST(first_frame_logs_the_connection_and_existing_alarms, "REQ-032")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_FAULT, 5000);
    status.alarms_latched = BOILER_FAULT_BIT(BOILER_FAULT_NO_FLOW);

    boiler_app_ingest(&app, &status, 0);

    SP_ASSERT_EQ_INT(2, boiler_notify_count(&app.notifications));
    SP_ASSERT(strcmp(boiler_notify_get(&app.notifications, 1)->text, "Controller connected") == 0);
    SP_ASSERT(strcmp(newest()->text, "No flow") == 0);
}

SP_TEST(state_change_is_logged_with_controller_time, "REQ-032")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_STANDBY, 1000);
    boiler_app_ingest(&app, &status, 0);

    status.state = BOILER_STATE_STARTUP;
    status.uptime_ms = 4200;
    boiler_app_ingest(&app, &status, 100);

    SP_ASSERT(strcmp(newest()->text, "State: Starting") == 0);
    SP_ASSERT_EQ_INT(4200, newest()->uptime_ms);
    SP_ASSERT_EQ_INT(SP_SEVERITY_INFO, newest()->severity);
}

SP_TEST(alarm_raise_and_clear_are_logged_with_severity, "REQ-032")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 1000);
    boiler_app_ingest(&app, &status, 0);

    status.alarms_active = BOILER_FAULT_BIT(BOILER_FAULT_TEMP_HIGH);
    boiler_app_ingest(&app, &status, 100);
    SP_ASSERT(strcmp(newest()->text, "High temperature") == 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_WARNING, newest()->severity);

    status.alarms_active = 0;
    boiler_app_ingest(&app, &status, 200);
    SP_ASSERT(strcmp(newest()->text, "High temperature cleared") == 0);
    SP_ASSERT_EQ_INT(SP_SEVERITY_NORMAL, newest()->severity);
}

SP_TEST(latched_alarm_is_not_logged_as_cleared_while_latched, "REQ-032")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 1000);
    boiler_app_ingest(&app, &status, 0);
    status.alarms_active = BOILER_FAULT_BIT(BOILER_FAULT_OVER_TEMP);
    boiler_app_ingest(&app, &status, 100);
    const size_t before = boiler_notify_count(&app.notifications);

    status.alarms_active = 0;
    status.alarms_latched = BOILER_FAULT_BIT(BOILER_FAULT_OVER_TEMP);
    boiler_app_ingest(&app, &status, 200);

    SP_ASSERT_EQ_INT(before, boiler_notify_count(&app.notifications));
}

SP_TEST(acknowledgement_is_logged, "REQ-032")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_FAULT, 1000);
    status.alarms_active = BOILER_FAULT_BIT(BOILER_FAULT_OVER_TEMP);
    status.alarms_unacked = status.alarms_active;
    boiler_app_ingest(&app, &status, 0);

    status.alarms_unacked = 0;
    boiler_app_ingest(&app, &status, 100);

    SP_ASSERT(strcmp(newest()->text, "Alarms acknowledged") == 0);
}

SP_TEST(newest_entry_comes_first_and_log_is_bounded, "REQ-032")
{
    BoilerNotifications log;
    char text[16];
    boiler_notify_init(&log);

    for (int i = 0; i < BOILER_NOTIFY_CAPACITY + 10; i++) {
        snprintf(text, sizeof text, "event %d", i);
        boiler_notify_add(&log, (uint32_t)i, SP_SEVERITY_NORMAL, text);
    }

    SP_ASSERT_EQ_INT(BOILER_NOTIFY_CAPACITY, boiler_notify_count(&log));
    SP_ASSERT(strcmp(boiler_notify_get(&log, 0)->text, "event 73") == 0);
    SP_ASSERT(strcmp(boiler_notify_get(&log, BOILER_NOTIFY_CAPACITY - 1)->text, "event 10") == 0);
    SP_ASSERT(boiler_notify_get(&log, BOILER_NOTIFY_CAPACITY) == NULL);
}

SP_TEST(long_text_is_truncated_not_overrun, "REQ-032")
{
    BoilerNotifications log;
    char text[200];
    memset(text, 'x', sizeof text - 1);
    text[sizeof text - 1] = '\0';
    boiler_notify_init(&log);

    boiler_notify_add(&log, 0, SP_SEVERITY_NORMAL, text);

    SP_ASSERT_EQ_INT(BOILER_NOTIFY_TEXT - 1, strlen(boiler_notify_get(&log, 0)->text));
}

/* REQ-033: controls */

static BoilerControlsView controls_in(BoilerState state, uint32_t unacked)
{
    BoilerStatus status = healthy_status(state, 1000);

    boiler_app_init(&app);
    status.alarms_active = unacked;
    status.alarms_unacked = unacked;
    boiler_app_ingest(&app, &status, 0);
    return view_of(&app).controls;
}

SP_TEST(start_is_enabled_only_in_standby, "REQ-033")
{
    SP_ASSERT(controls_in(BOILER_STATE_STANDBY, 0).start);
    SP_ASSERT(!controls_in(BOILER_STATE_INIT, 0).start);
    SP_ASSERT(!controls_in(BOILER_STATE_STARTUP, 0).start);
    SP_ASSERT(!controls_in(BOILER_STATE_RUNNING, 0).start);
    SP_ASSERT(!controls_in(BOILER_STATE_SHUTDOWN, 0).start);
    SP_ASSERT(!controls_in(BOILER_STATE_FAULT, 0).start);
}

SP_TEST(stop_is_enabled_only_while_starting_or_running, "REQ-033")
{
    SP_ASSERT(controls_in(BOILER_STATE_STARTUP, 0).stop);
    SP_ASSERT(controls_in(BOILER_STATE_RUNNING, 0).stop);
    SP_ASSERT(!controls_in(BOILER_STATE_STANDBY, 0).stop);
    SP_ASSERT(!controls_in(BOILER_STATE_SHUTDOWN, 0).stop);
    SP_ASSERT(!controls_in(BOILER_STATE_FAULT, 0).stop);
}

SP_TEST(reset_is_enabled_only_in_fault, "REQ-033")
{
    SP_ASSERT(controls_in(BOILER_STATE_FAULT, 0).reset);
    SP_ASSERT(!controls_in(BOILER_STATE_RUNNING, 0).reset);
    SP_ASSERT(!controls_in(BOILER_STATE_STANDBY, 0).reset);
}

SP_TEST(manual_pump_and_valve_are_enabled_only_in_standby, "REQ-033")
{
    SP_ASSERT(controls_in(BOILER_STATE_STANDBY, 0).pump_toggle);
    SP_ASSERT(controls_in(BOILER_STATE_STANDBY, 0).valve_toggle);
    SP_ASSERT(!controls_in(BOILER_STATE_RUNNING, 0).pump_toggle);
    SP_ASSERT(!controls_in(BOILER_STATE_FAULT, 0).valve_toggle);
}

SP_TEST(acknowledge_is_enabled_only_with_unacknowledged_alarms, "REQ-033")
{
    SP_ASSERT(controls_in(BOILER_STATE_FAULT, BOILER_FAULT_BIT(BOILER_FAULT_NO_FLOW)).acknowledge);
    SP_ASSERT(!controls_in(BOILER_STATE_FAULT, 0).acknowledge);
}

SP_TEST(actions_map_to_single_commands, "REQ-033")
{
    const BoilerCommands start = boiler_action_start();
    const BoilerCommands reset = boiler_action_reset();
    BoilerCommands zero = {0};

    SP_ASSERT_EQ_INT(1, start.start);
    zero.start = 1;
    SP_ASSERT(memcmp(&start, &zero, sizeof zero) == 0);
    SP_ASSERT_EQ_INT(1, reset.reset);
    SP_ASSERT_EQ_INT(1, boiler_action_stop().stop);
    SP_ASSERT_EQ_INT(1, boiler_action_acknowledge().ack);
}

SP_TEST(toggle_requests_the_opposite_of_the_current_state, "REQ-033")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_STANDBY, 1000);
    status.pump_on = 0;
    status.valve_open = 1;
    boiler_app_ingest(&app, &status, 0);

    SP_ASSERT_EQ_INT(1, boiler_action_toggle_pump(&app).pump_manual);
    SP_ASSERT_EQ_INT(2, boiler_action_toggle_valve(&app).valve_manual);
}

SP_TEST(setpoint_change_is_limited_to_the_configured_range, "REQ-033")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_STANDBY, 1000);
    status.setpoint_c = 88.0f;
    boiler_app_ingest(&app, &status, 0);

    SP_ASSERT_NEAR(89.0, boiler_action_change_setpoint(&app, 1.0f).setpoint_c, 0);
    SP_ASSERT_NEAR(90.0, boiler_action_change_setpoint(&app, 5.0f).setpoint_c, 0);
    SP_ASSERT_NEAR(40.0, boiler_action_change_setpoint(&app, -100.0f).setpoint_c, 0);
    SP_ASSERT_EQ_INT(1, boiler_action_change_setpoint(&app, 1.0f).set_setpoint);
}

/* REQ-034: link supervision */

SP_TEST(link_is_up_after_a_frame_and_down_after_two_seconds_of_silence, "REQ-034")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 1000);
    boiler_app_ingest(&app, &status, 10000);

    boiler_app_tick(&app, 12000);
    SP_ASSERT(app.link_up);

    boiler_app_tick(&app, 12001);
    SP_ASSERT(!app.link_up);
    SP_ASSERT(strcmp(newest()->text, "Link lost") == 0);
}

SP_TEST(controls_are_disabled_while_the_link_is_down, "REQ-034")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_STANDBY, 1000);
    boiler_app_ingest(&app, &status, 0);
    boiler_app_tick(&app, 5000);

    const BoilerViewModel view = view_of(&app);

    SP_ASSERT(!view.controls.start && !view.controls.pump_toggle && !view.controls.setpoint_adjust);
    SP_ASSERT(strcmp(view.state_text, "No link") == 0);
}

SP_TEST(link_lost_is_logged_once, "REQ-034")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_RUNNING, 1000);
    boiler_app_ingest(&app, &status, 0);
    boiler_app_tick(&app, 5000);
    const size_t after_first = boiler_notify_count(&app.notifications);

    boiler_app_tick(&app, 9000);

    SP_ASSERT_EQ_INT(after_first, boiler_notify_count(&app.notifications));
}

SP_TEST(link_resumes_when_frames_return, "REQ-034")
{
    boiler_app_init(&app);
    BoilerStatus status = healthy_status(BOILER_STATE_STANDBY, 1000);
    boiler_app_ingest(&app, &status, 0);
    boiler_app_tick(&app, 5000);

    boiler_app_ingest(&app, &status, 6000);

    SP_ASSERT(app.link_up);
    SP_ASSERT(view_of(&app).controls.start);
    SP_ASSERT(strcmp(newest()->text, "Link restored") == 0);
}

SP_TEST(nothing_is_shown_before_the_first_frame, "REQ-034")
{
    boiler_app_init(&app);
    boiler_app_tick(&app, 100000);

    const BoilerViewModel view = view_of(&app);

    SP_ASSERT(!view.link_up);
    SP_ASSERT(strcmp(view.temperature.text, "--") == 0);
    SP_ASSERT(!view.controls.start);
}

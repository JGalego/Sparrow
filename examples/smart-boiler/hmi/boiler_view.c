#include "boiler_view.h"

#include <stdio.h>
#include <string.h>

#define BIT(fault) BOILER_FAULT_BIT(BOILER_FAULT_##fault)

static uint32_t standing_alarms(const BoilerStatus *status)
{
    return status->alarms_active | status->alarms_latched;
}

static SpSeverity severity_of(uint32_t standing, uint32_t critical_bits, uint32_t warning_bits)
{
    if (standing & critical_bits) {
        return SP_SEVERITY_CRITICAL;
    }
    if (standing & warning_bits) {
        return SP_SEVERITY_WARNING;
    }
    return SP_SEVERITY_NORMAL;
}

static void build_value(BoilerValueView *view, float value, bool valid, const char *format,
                        SpSeverity severity)
{
    view->valid = valid;
    view->value = value;
    if (valid) {
        snprintf(view->text, sizeof view->text, format, (double)value);
        view->severity = severity;
    } else {
        snprintf(view->text, sizeof view->text, "--");
        view->severity = SP_SEVERITY_INACTIVE;
    }
}

static SpSeverity state_severity(BoilerState state)
{
    switch (state) {
    case BOILER_STATE_RUNNING:
        return SP_SEVERITY_NORMAL;
    case BOILER_STATE_STARTUP:
    case BOILER_STATE_SHUTDOWN:
        return SP_SEVERITY_INFO;
    case BOILER_STATE_FAULT:
        return SP_SEVERITY_CRITICAL;
    case BOILER_STATE_INIT:
    case BOILER_STATE_STANDBY:
    case BOILER_STATE_COUNT:
        break;
    }
    return SP_SEVERITY_INACTIVE;
}

static void build_headline(BoilerViewModel *view, uint32_t standing)
{
    view->alarm_count = 0;
    view->alarm_headline[0] = '\0';
    view->alarm_severity = SP_SEVERITY_INACTIVE;

    for (int fault = 0; fault < BOILER_FAULT_COUNT; fault++) {
        const BoilerFaultInfo *info = boiler_fault_info((BoilerFault)fault);
        const bool critical = info->severity == BOILER_SEVERITY_CRITICAL;

        if (!(standing & BOILER_FAULT_BIT(fault))) {
            continue;
        }
        view->alarm_count++;
        if (critical && view->alarm_severity != SP_SEVERITY_CRITICAL) {
            view->alarm_severity = SP_SEVERITY_CRITICAL;
            snprintf(view->alarm_headline, sizeof view->alarm_headline, "%s", info->text);
        } else if (!critical && view->alarm_severity == SP_SEVERITY_INACTIVE) {
            view->alarm_severity = SP_SEVERITY_WARNING;
            snprintf(view->alarm_headline, sizeof view->alarm_headline, "%s", info->text);
        }
    }
}

static int count_bits(uint32_t mask)
{
    int count = 0;

    for (; mask != 0; mask &= mask - 1) {
        count++;
    }
    return count;
}

static void build_controls(BoilerViewModel *view, const BoilerStatus *status, uint32_t standing)
{
    const BoilerState state = (BoilerState)status->state;
    const bool link = view->link_up;
    BoilerControlsView *controls = &view->controls;

    controls->start = link && state == BOILER_STATE_STANDBY;
    controls->stop = link && (state == BOILER_STATE_STARTUP || state == BOILER_STATE_RUNNING);
    controls->reset = link && state == BOILER_STATE_FAULT;
    controls->acknowledge = link && status->alarms_unacked != 0 && standing != 0;
    controls->pump_toggle = link && state == BOILER_STATE_STANDBY;
    controls->valve_toggle = link && state == BOILER_STATE_STANDBY;
    controls->setpoint_adjust = link && state != BOILER_STATE_INIT;
}

static void build_process_values(BoilerViewModel *view, const BoilerStatus *status,
                                 uint32_t standing)
{
    build_value(&view->temperature, status->temperature_c, status->temperature_valid, "%.1f",
                severity_of(standing, BIT(OVER_TEMP), BIT(TEMP_HIGH)));
    build_value(&view->pressure, status->pressure_bar, status->pressure_valid, "%.2f",
                severity_of(standing, BIT(OVER_PRESSURE) | BIT(LOW_PRESSURE), BIT(PRESSURE_HIGH)));
    build_value(&view->flow, status->flow_lpm, status->flow_valid, "%.1f",
                severity_of(standing, BIT(NO_FLOW), 0));
    build_value(&view->valve_position, status->valve_position_pct, status->valve_valid, "%.0f",
                severity_of(standing, BIT(VALVE_FAILURE), 0));
    build_value(&view->heater_power, status->heater_power_pct, true, "%.0f", SP_SEVERITY_NORMAL);
    view->heater_power.severity =
        status->heater_contactor ? SP_SEVERITY_NORMAL : SP_SEVERITY_INACTIVE;
}

static void format_uptime(char *text, size_t size, uint32_t uptime_ms)
{
    const uint32_t seconds = uptime_ms / 1000;

    snprintf(text, size, "%02u:%02u:%02u", (unsigned)(seconds / 3600),
             (unsigned)(seconds / 60 % 60), (unsigned)(seconds % 60));
}

void boiler_view_build(BoilerViewModel *view, const BoilerApp *app)
{
    const BoilerStatus *status = &app->status;
    const uint32_t standing = standing_alarms(status);

    memset(view, 0, sizeof *view);
    view->link_up = app->link_up;
    if (!app->have_status) {
        snprintf(view->state_text, sizeof view->state_text, "No link");
        snprintf(view->temperature.text, sizeof view->temperature.text, "--");
        snprintf(view->pressure.text, sizeof view->pressure.text, "--");
        snprintf(view->flow.text, sizeof view->flow.text, "--");
        snprintf(view->valve_position.text, sizeof view->valve_position.text, "--");
        snprintf(view->heater_power.text, sizeof view->heater_power.text, "--");
        snprintf(view->setpoint_text, sizeof view->setpoint_text, "--");
        snprintf(view->uptime_text, sizeof view->uptime_text, "--:--:--");
        return;
    }

    build_process_values(view, status, standing);
    snprintf(view->setpoint_text, sizeof view->setpoint_text, "%.0f", (double)status->setpoint_c);
    snprintf(view->state_text, sizeof view->state_text, "%s",
             app->link_up ? boiler_state_label((BoilerState)status->state) : "No link");
    view->state_severity =
        app->link_up ? state_severity((BoilerState)status->state) : SP_SEVERITY_WARNING;
    format_uptime(view->uptime_text, sizeof view->uptime_text, status->uptime_ms);

    view->pump_on = status->pump_on;
    view->valve_open = status->valve_open;
    view->heater_on = status->heater_contactor && status->heater_power_pct > 0.0f;
    view->pump_severity = severity_of(standing, BIT(PUMP_FAILURE), 0);
    view->valve_severity = severity_of(standing, BIT(VALVE_FAILURE), 0);

    build_headline(view, standing);
    view->unacknowledged_count = count_bits(status->alarms_unacked);
    build_controls(view, status, standing);
}

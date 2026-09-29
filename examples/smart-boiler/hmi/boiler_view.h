#ifndef BOILER_VIEW_H
#define BOILER_VIEW_H

#include <stdbool.h>

#include "boiler_app.h"
#include "sp_severity.h"

#define BOILER_TEXT 24

/* One process value as the screen shows it. */
typedef struct {
    char text[BOILER_TEXT]; /* formatted number, or "--" when the reading is invalid */
    SpSeverity severity;
    float value;
    bool valid;
} BoilerValueView;

typedef struct {
    bool start;
    bool stop;
    bool reset;
    bool acknowledge;
    bool pump_toggle;
    bool valve_toggle;
    bool setpoint_adjust;
} BoilerControlsView;

/*
 * Screen contents derived from BoilerApp. Widgets render this structure and
 * contain no rules of their own about what is critical or enabled.
 */
typedef struct {
    BoilerValueView temperature;
    BoilerValueView pressure;
    BoilerValueView flow;
    BoilerValueView valve_position;
    BoilerValueView heater_power;

    char setpoint_text[BOILER_TEXT];
    char state_text[BOILER_TEXT];
    SpSeverity state_severity;
    char uptime_text[BOILER_TEXT];

    SpSeverity pump_severity;
    SpSeverity valve_severity;
    bool pump_on;
    bool valve_open;
    bool heater_on;

    int alarm_count;
    int unacknowledged_count;
    SpSeverity alarm_severity;
    char alarm_headline[BOILER_NOTIFY_TEXT];

    bool link_up;
    BoilerControlsView controls;
} BoilerViewModel;

void boiler_view_build(BoilerViewModel *view, const BoilerApp *app);

#endif

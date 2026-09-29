#include "boiler_screen.h"

#include <math.h>
#include <stdio.h>

#include "boiler_actions.h"
#include "boiler_process_view.h"
#include "boiler_sim_panel.h"
#include "boiler_view.h"
#include "sp_button.h"
#include "sp_gauge.h"
#include "sp_notification_list.h"
#include "sp_panel.h"
#include "sp_pill.h"
#include "sp_theme.h"
#include "sp_trend.h"

#define NOTIFICATION_ROWS      4
#define SETPOINT_STEP_C        1.0f
#define COLOR_TEMPERATURE_LINE lv_color_hex(0xF2994A)
#define COLOR_PRESSURE_LINE    lv_color_hex(0x4FA8F0)

struct BoilerScreen {
    const BoilerApp *app;
    BoilerScreenHost host;
    BoilerChartRange range;
    uint32_t drawn_history_second;
    bool chart_dirty;

    lv_obj_t *state_pill;
    lv_obj_t *alarm_pill;
    lv_obj_t *uptime_label;
    lv_obj_t *link_dot;
    lv_obj_t *sim_button;
    lv_obj_t *sim_panel;

    lv_obj_t *process;
    lv_obj_t *temperature_gauge;
    lv_obj_t *pressure_gauge;
    lv_obj_t *setpoint_label;
    lv_obj_t *setpoint_down;
    lv_obj_t *setpoint_up;

    lv_obj_t *trend;
    lv_obj_t *range_buttons[3];

    lv_obj_t *start_button;
    lv_obj_t *stop_button;
    lv_obj_t *ack_button;
    lv_obj_t *reset_button;
    lv_obj_t *pump_button;
    lv_obj_t *valve_button;

    lv_obj_t *notifications;
};

static void send(BoilerScreen *screen, BoilerCommands commands)
{
    screen->host.send_command(screen->host.context, &commands);
}

/* Actions */

static void on_start(lv_event_t *e)
{
    send(lv_event_get_user_data(e), boiler_action_start());
}

static void on_stop(lv_event_t *e)
{
    send(lv_event_get_user_data(e), boiler_action_stop());
}

static void on_acknowledge(lv_event_t *e)
{
    send(lv_event_get_user_data(e), boiler_action_acknowledge());
}

static void on_reset(lv_event_t *e)
{
    send(lv_event_get_user_data(e), boiler_action_reset());
}

static void on_pump(lv_event_t *e)
{
    BoilerScreen *screen = lv_event_get_user_data(e);

    send(screen, boiler_action_toggle_pump(screen->app));
}

static void on_valve(lv_event_t *e)
{
    BoilerScreen *screen = lv_event_get_user_data(e);

    send(screen, boiler_action_toggle_valve(screen->app));
}

static void on_setpoint_up(lv_event_t *e)
{
    BoilerScreen *screen = lv_event_get_user_data(e);

    send(screen, boiler_action_change_setpoint(screen->app, SETPOINT_STEP_C));
}

static void on_setpoint_down(lv_event_t *e)
{
    BoilerScreen *screen = lv_event_get_user_data(e);

    send(screen, boiler_action_change_setpoint(screen->app, -SETPOINT_STEP_C));
}

static void on_sim_button(lv_event_t *e)
{
    BoilerScreen *screen = lv_event_get_user_data(e);

    boiler_screen_show_sim_panel(screen, lv_obj_has_flag(screen->sim_panel, LV_OBJ_FLAG_HIDDEN));
}

static void on_range(lv_event_t *e)
{
    BoilerScreen *screen = lv_event_get_user_data(e);
    lv_obj_t *button = lv_event_get_target_obj(e);
    static const BoilerChartRange ranges[3] = {BOILER_CHART_2_MIN, BOILER_CHART_10_MIN,
                                               BOILER_CHART_60_MIN};

    for (int i = 0; i < 3; i++) {
        if (screen->range_buttons[i] == button) {
            boiler_screen_set_range(screen, ranges[i]);
        }
    }
}

/* Construction */

static void fill_circle(lv_layer_t *layer, int32_t cx, int32_t cy, int32_t radius, lv_color_t color)
{
    lv_draw_rect_dsc_t dsc;
    const lv_area_t area = {cx - radius, cy - radius, cx + radius, cy + radius};

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.radius = LV_RADIUS_CIRCLE;
    lv_draw_rect(layer, &dsc, &area);
}

static void fill_triangle(lv_layer_t *layer, const lv_point_t points[3], lv_color_t color)
{
    lv_draw_triangle_dsc_t dsc;

    lv_draw_triangle_dsc_init(&dsc);
    dsc.bg_color = color;
    for (int i = 0; i < 3; i++) {
        dsc.p[i].x = points[i].x;
        dsc.p[i].y = points[i].y;
    }
    lv_draw_triangle(layer, &dsc);
}

/* The logo mark (docs/assets/logo.svg) reduced to primitives: body, head, tail, conical bill. */
static void on_draw_mark(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target_obj(event), &area);
    const int32_t x = area.x1;
    const int32_t y = area.y1;
    const lv_point_t tail[3] = {{x + 8, y + 16}, {x + 12, y + 23}, {x + 1, y + 28}};
    const lv_point_t beak[3] = {{x + 26, y + 6}, {x + 30, y + 9}, {x + 26, y + 11}};

    fill_triangle(layer, tail, SP_COLOR_BRAND);
    fill_circle(layer, x + 14, y + 18, 9, SP_COLOR_BRAND);
    fill_circle(layer, x + 21, y + 9, 6, SP_COLOR_BRAND);
    fill_triangle(layer, beak, sp_severity_color(SP_SEVERITY_WARNING));
    fill_circle(layer, x + 23, y + 8, 1, SP_COLOR_BACKGROUND);
}

static void build_top_bar(BoilerScreen *screen, lv_obj_t *root, bool sim_tools)
{
    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_t *mark = lv_obj_create(bar);
    lv_obj_t *name = sp_label_create(bar, SP_FONT_TITLE, SP_COLOR_TEXT);
    lv_obj_t *product = sp_label_create(bar, SP_FONT_LABEL, SP_COLOR_TEXT_DIM);

    sp_theme_make_plain(bar);
    lv_obj_set_size(bar, BOILER_SCREEN_WIDTH, 56);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, SP_COLOR_SURFACE, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, SP_COLOR_BORDER, 0);

    sp_theme_make_plain(mark);
    lv_obj_set_size(mark, 32, 30);
    lv_obj_set_pos(mark, 14, 13);
    lv_obj_add_event_cb(mark, on_draw_mark, LV_EVENT_DRAW_MAIN, NULL);
    lv_label_set_text(name, "Sparrow");
    lv_obj_set_pos(name, 54, 15);
    lv_label_set_text(product, "Smart Boiler");
    lv_obj_set_pos(product, 152, 18);

    screen->state_pill = sp_pill_create(bar, SP_FONT_LABEL, 422, 12, 180, 32);
    screen->alarm_pill = sp_pill_create(bar, SP_FONT_BODY, 626, 12, 250, 32);
    screen->uptime_label = sp_label_create(bar, SP_FONT_BODY, SP_COLOR_TEXT_DIM);
    lv_obj_set_pos(screen->uptime_label, 896, 19);
    screen->link_dot = lv_obj_create(bar);
    sp_theme_make_plain(screen->link_dot);
    lv_obj_set_size(screen->link_dot, 10, 10);
    lv_obj_set_pos(screen->link_dot, 996, 23);
    lv_obj_set_style_radius(screen->link_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(screen->link_dot, LV_OPA_COVER, 0);

    if (sim_tools) {
        screen->sim_button = sp_button_create(bar, "SIM", SP_BUTTON_NEUTRAL, 300, 12, 64, 32);
        lv_obj_add_event_cb(screen->sim_button, on_sim_button, LV_EVENT_CLICKED, screen);
    }
}

static void build_setpoint_stepper(BoilerScreen *screen, lv_obj_t *panel)
{
    screen->setpoint_down =
        sp_button_create(panel, LV_SYMBOL_MINUS, SP_BUTTON_NEUTRAL, 16, 252, 44, 36);
    screen->setpoint_up =
        sp_button_create(panel, LV_SYMBOL_PLUS, SP_BUTTON_NEUTRAL, 180, 252, 44, 36);
    screen->setpoint_label = sp_label_create(panel, SP_FONT_LABEL, SP_COLOR_TEXT);
    lv_obj_set_width(screen->setpoint_label, 112);
    lv_obj_set_style_text_align(screen->setpoint_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(screen->setpoint_label, 64, 262);
    lv_obj_add_event_cb(screen->setpoint_down, on_setpoint_down, LV_EVENT_CLICKED, screen);
    lv_obj_add_event_cb(screen->setpoint_up, on_setpoint_up, LV_EVENT_CLICKED, screen);
}

static void build_gauges(BoilerScreen *screen, lv_obj_t *root)
{
    const BoilerConfig config = boiler_config_default();
    const SpGaugeSpec temperature = {.min = 0.0f,
                                     .max = 120.0f,
                                     .warning_from = config.temperature_warn_c,
                                     .critical_from = config.temperature_trip_c,
                                     .critical_below = NAN,
                                     .unit = "\xC2\xB0"
                                             "C"};
    const SpGaugeSpec pressure = {.min = 0.0f,
                                  .max = 5.0f,
                                  .warning_from = config.pressure_warn_bar,
                                  .critical_from = config.pressure_trip_bar,
                                  .critical_below = config.pressure_low_bar,
                                  .unit = "bar"};
    lv_obj_t *temperature_panel = sp_panel_create(root, "TEMPERATURE", 524, 68, 240, 300);
    lv_obj_t *pressure_panel = sp_panel_create(root, "PRESSURE", 776, 68, 236, 300);
    lv_obj_t *limits = sp_label_create(pressure_panel, SP_FONT_SMALL, SP_COLOR_TEXT_DIM);
    char text[64];

    screen->temperature_gauge = sp_gauge_create(temperature_panel, &temperature, 208);
    lv_obj_set_pos(screen->temperature_gauge, 16, 40);
    screen->pressure_gauge = sp_gauge_create(pressure_panel, &pressure, 204);
    lv_obj_set_pos(screen->pressure_gauge, 16, 40);
    build_setpoint_stepper(screen, temperature_panel);

    snprintf(text, sizeof text, "Warning %.1f bar   Trip %.1f bar   Low %.1f bar",
             (double)config.pressure_warn_bar, (double)config.pressure_trip_bar,
             (double)config.pressure_low_bar);
    lv_label_set_text(limits, text);
    lv_obj_set_width(limits, 204);
    lv_obj_set_style_text_align(limits, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(limits, 16, 258);
}

static void build_history(BoilerScreen *screen, lv_obj_t *root)
{
    static const char *const names[3] = {"2 min", "10 min", "1 h"};
    const SpTrendSpec spec = {
        .left_min = 0.0f,
        .left_max = 120.0f,
        .right_min = 0.0f,
        .right_max = 6.0f,
        .colors = {COLOR_TEMPERATURE_LINE, SP_COLOR_TEXT_DIM, COLOR_PRESSURE_LINE}};
    lv_obj_t *panel = sp_panel_create(root, "HISTORY", 12, 380, 500, 208);
    lv_obj_t *temperature = sp_label_create(panel, SP_FONT_SMALL, COLOR_TEMPERATURE_LINE);
    lv_obj_t *pressure = sp_label_create(panel, SP_FONT_SMALL, COLOR_PRESSURE_LINE);

    lv_label_set_text(temperature, "Temperature \xC2\xB0"
                                   "C");
    lv_obj_set_pos(temperature, 92, 12);
    lv_label_set_text(pressure, "Pressure bar");
    lv_obj_set_pos(pressure, 196, 12);
    screen->trend = sp_trend_create(panel, &spec, 8, 40, 484, 160);
    for (int i = 0; i < 3; i++) {
        screen->range_buttons[i] = sp_button_create(panel, names[i], SP_BUTTON_NEUTRAL,
                                                    500 - 16 - (3 - i) * 58 + 6, 6, 52, 26);
        lv_obj_set_style_text_font(screen->range_buttons[i], SP_FONT_SMALL, 0);
        lv_obj_add_event_cb(screen->range_buttons[i], on_range, LV_EVENT_CLICKED, screen);
    }
}

static lv_obj_t *add_button(BoilerScreen *screen, lv_obj_t *panel, const char *text,
                            SpButtonKind kind, int32_t x, int32_t y, int32_t height,
                            lv_event_cb_t callback)
{
    lv_obj_t *button = sp_button_create(panel, text, kind, x, y, 100, height);

    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, screen);
    return button;
}

static void build_controls(BoilerScreen *screen, lv_obj_t *root)
{
    lv_obj_t *panel = sp_panel_create(root, "CONTROLS", 524, 380, 240, 208);

    screen->start_button =
        add_button(screen, panel, "START", SP_BUTTON_PRIMARY, 16, 40, 44, on_start);
    screen->stop_button = add_button(screen, panel, "STOP", SP_BUTTON_DANGER, 124, 40, 44, on_stop);
    screen->ack_button =
        add_button(screen, panel, "ACK", SP_BUTTON_NEUTRAL, 16, 92, 38, on_acknowledge);
    screen->reset_button =
        add_button(screen, panel, "RESET", SP_BUTTON_NEUTRAL, 124, 92, 38, on_reset);
    screen->pump_button =
        add_button(screen, panel, "PUMP", SP_BUTTON_NEUTRAL, 16, 142, 50, on_pump);
    screen->valve_button =
        add_button(screen, panel, "VALVE", SP_BUTTON_NEUTRAL, 124, 142, 50, on_valve);
}

BoilerScreen *boiler_screen_create(lv_obj_t *parent, const BoilerApp *app,
                                   const BoilerScreenHost *host, bool sim_tools)
{
    BoilerScreen *screen = lv_malloc(sizeof *screen);
    lv_obj_t *notification_panel;

    lv_memzero(screen, sizeof *screen);
    screen->app = app;
    screen->host = *host;
    screen->range = BOILER_CHART_10_MIN;
    screen->chart_dirty = true;

    lv_obj_set_style_bg_color(parent, SP_COLOR_BACKGROUND, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    build_top_bar(screen, parent, sim_tools);

    {
        lv_obj_t *process_panel = sp_panel_create(parent, "PROCESS", 12, 68, 500, 300);
        screen->process = boiler_process_view_create(process_panel, 0, 30, 500, 270);
    }
    build_gauges(screen, parent);
    build_history(screen, parent);
    build_controls(screen, parent);

    notification_panel = sp_panel_create(parent, "NOTIFICATIONS", 776, 380, 236, 208);
    screen->notifications =
        sp_notification_list_create(notification_panel, 16, 36, 208, 168, NOTIFICATION_ROWS);
    if (sim_tools) {
        screen->sim_panel = boiler_sim_panel_create(parent, &screen->host);
    }
    boiler_screen_set_range(screen, screen->range);
    return screen;
}

/* Updates */

static void format_time(char *text, size_t size, uint32_t uptime_ms)
{
    const uint32_t seconds = uptime_ms / 1000;

    snprintf(text, size, "%02u:%02u:%02u", (unsigned)(seconds / 3600),
             (unsigned)(seconds / 60 % 60), (unsigned)(seconds % 60));
}

static void update_top_bar(BoilerScreen *screen, const BoilerViewModel *view)
{
    char headline[BOILER_NOTIFY_TEXT + 16];

    sp_pill_set(screen->state_pill, view->state_text, view->state_severity);
    if (view->alarm_count == 0) {
        sp_pill_set(screen->alarm_pill, "No active alarms", SP_SEVERITY_INACTIVE);
    } else {
        snprintf(headline, sizeof headline, view->alarm_count > 1 ? "%s  +%d" : "%s",
                 view->alarm_headline, view->alarm_count - 1);
        sp_pill_set(screen->alarm_pill, headline, view->alarm_severity);
    }
    lv_label_set_text(screen->uptime_label, view->uptime_text);
    lv_obj_set_style_bg_color(
        screen->link_dot,
        sp_severity_color(view->link_up ? SP_SEVERITY_NORMAL : SP_SEVERITY_CRITICAL), 0);
}

static void update_gauges(BoilerScreen *screen, const BoilerViewModel *view)
{
    const BoilerStatus *status = &screen->app->status;
    char text[BOILER_TEXT + 8];

    sp_gauge_set_value(screen->temperature_gauge, view->temperature.value, view->temperature.valid,
                       view->temperature.text, view->temperature.severity);
    sp_gauge_set_marker(screen->temperature_gauge, status->setpoint_c, screen->app->have_status);
    sp_gauge_set_value(screen->pressure_gauge, view->pressure.value, view->pressure.valid,
                       view->pressure.text, view->pressure.severity);
    snprintf(text, sizeof text,
             "%s \xC2\xB0"
             "C",
             view->setpoint_text);
    lv_label_set_text(screen->setpoint_label, text);
}

static void set_tile(lv_obj_t *button, const char *name, const char *state, SpSeverity severity)
{
    lv_obj_t *label = lv_obj_get_child(button, 0);

    lv_label_set_text_fmt(label, "%s\n%s", name, state);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(button, sp_severity_color(severity), 0);
}

static void update_controls(BoilerScreen *screen, const BoilerViewModel *view)
{
    const BoilerControlsView *controls = &view->controls;

    sp_button_set_enabled(screen->start_button, controls->start);
    sp_button_set_enabled(screen->stop_button, controls->stop);
    sp_button_set_enabled(screen->ack_button, controls->acknowledge);
    sp_button_set_enabled(screen->reset_button, controls->reset);
    sp_button_set_enabled(screen->pump_button, controls->pump_toggle);
    sp_button_set_enabled(screen->valve_button, controls->valve_toggle);
    sp_button_set_enabled(screen->setpoint_up, controls->setpoint_adjust);
    sp_button_set_enabled(screen->setpoint_down, controls->setpoint_adjust);
    set_tile(screen->pump_button, "PUMP", view->pump_on ? "ON" : "OFF",
             view->pump_severity == SP_SEVERITY_CRITICAL
                 ? SP_SEVERITY_CRITICAL
                 : (view->pump_on ? SP_SEVERITY_NORMAL : SP_SEVERITY_INACTIVE));
    set_tile(screen->valve_button, "VALVE", view->valve_open ? "OPEN" : "CLOSED",
             view->valve_severity == SP_SEVERITY_CRITICAL
                 ? SP_SEVERITY_CRITICAL
                 : (view->valve_open ? SP_SEVERITY_NORMAL : SP_SEVERITY_INACTIVE));
}

static void update_notifications(BoilerScreen *screen)
{
    const BoilerNotifications *log = &screen->app->notifications;
    char time_text[BOILER_TEXT];

    for (int row = 0; row < NOTIFICATION_ROWS; row++) {
        const BoilerNotification *entry = boiler_notify_get(log, (size_t)row);

        if (entry == NULL) {
            sp_notification_list_set_row(screen->notifications, row, NULL, NULL,
                                         SP_SEVERITY_INACTIVE);
            continue;
        }
        format_time(time_text, sizeof time_text, entry->uptime_ms);
        sp_notification_list_set_row(screen->notifications, row, entry->text, time_text,
                                     entry->severity);
    }
}

static void update_chart(BoilerScreen *screen)
{
    static const char *const span_text[] = {"-2 min", "-10 min", "-1 h"};
    float points[BOILER_CHART_POINTS];
    const int span_index =
        screen->range == BOILER_CHART_2_MIN ? 0 : (screen->range == BOILER_CHART_10_MIN ? 1 : 2);

    if (!screen->chart_dirty && screen->drawn_history_second == screen->app->last_history_second) {
        return;
    }
    for (int series = 0; series < SP_TREND_SERIES; series++) {
        static const BoilerSeries mapping[SP_TREND_SERIES] = {
            BOILER_SERIES_TEMPERATURE, BOILER_SERIES_SETPOINT, BOILER_SERIES_PRESSURE};
        boiler_app_series(screen->app, mapping[series], screen->range, points, BOILER_CHART_POINTS);
        sp_trend_set_series(screen->trend, series, points, BOILER_CHART_POINTS);
    }
    sp_trend_set_span_label(screen->trend, span_text[span_index]);
    screen->drawn_history_second = screen->app->last_history_second;
    screen->chart_dirty = false;
}

void boiler_screen_update(BoilerScreen *screen)
{
    BoilerViewModel view;

    boiler_view_build(&view, screen->app);
    update_top_bar(screen, &view);
    boiler_process_view_update(screen->process, &view);
    update_gauges(screen, &view);
    update_controls(screen, &view);
    update_notifications(screen);
    update_chart(screen);
}

void boiler_screen_set_range(BoilerScreen *screen, BoilerChartRange range)
{
    static const BoilerChartRange ranges[3] = {BOILER_CHART_2_MIN, BOILER_CHART_10_MIN,
                                               BOILER_CHART_60_MIN};

    screen->range = range;
    screen->chart_dirty = true;
    for (int i = 0; i < 3; i++) {
        const bool selected = ranges[i] == range;
        lv_obj_set_style_bg_color(screen->range_buttons[i],
                                  selected ? SP_COLOR_TRACK : SP_COLOR_SURFACE_RAISED, 0);
        lv_obj_set_style_text_color(screen->range_buttons[i],
                                    selected ? SP_COLOR_TEXT : SP_COLOR_TEXT_DIM, 0);
    }
}

void boiler_screen_show_sim_panel(BoilerScreen *screen, bool visible)
{
    if (screen->sim_panel == NULL) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(screen->sim_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen->sim_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

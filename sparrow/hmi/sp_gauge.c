#include "sp_gauge.h"

#include <math.h>

#include "sp_theme.h"

#define PI_F         3.14159265f
#define SWEEP_START  135.0f
#define SWEEP_SIZE   270.0f
#define ARC_WIDTH    12
#define ZONE_OPACITY LV_OPA_60

typedef struct {
    SpGaugeSpec spec;
    float value;
    bool valid;
    SpSeverity severity;
    float marker;
    bool marker_visible;
    lv_obj_t *value_label;
    lv_obj_t *unit_label;
} SpGauge;

static float angle_of(const SpGauge *gauge, float value)
{
    const float span = gauge->spec.max - gauge->spec.min;
    const float fraction = fminf(1.0f, fmaxf(0.0f, (value - gauge->spec.min) / span));

    return SWEEP_START + SWEEP_SIZE * fraction;
}

static void draw_arc(lv_layer_t *layer, lv_point_t center, uint16_t radius, float from, float to,
                     lv_color_t color, lv_opa_t opa, bool rounded)
{
    lv_draw_arc_dsc_t dsc;

    if (to - from < 0.5f) {
        return;
    }
    lv_draw_arc_dsc_init(&dsc);
    dsc.center = center;
    dsc.radius = radius;
    dsc.width = ARC_WIDTH;
    dsc.start_angle = SP_PRECISE(fmodf(from, 360.0f));
    dsc.end_angle = SP_PRECISE(fmodf(to, 360.0f));
    dsc.color = color;
    dsc.opa = opa;
    dsc.rounded = rounded;
    lv_draw_arc(layer, &dsc);
}

static void draw_zones(const SpGauge *gauge, lv_layer_t *layer, lv_point_t center, uint16_t radius)
{
    const SpGaugeSpec *spec = &gauge->spec;
    const lv_color_t amber = sp_severity_color(SP_SEVERITY_WARNING);
    const lv_color_t red = sp_severity_color(SP_SEVERITY_CRITICAL);
    const float end = SWEEP_START + SWEEP_SIZE;

    draw_arc(layer, center, radius, SWEEP_START, end, SP_COLOR_TRACK, LV_OPA_COVER, false);
    if (!isnan(spec->warning_from)) {
        const float to = isnan(spec->critical_from) ? end : angle_of(gauge, spec->critical_from);
        draw_arc(layer, center, radius, angle_of(gauge, spec->warning_from), to, amber,
                 ZONE_OPACITY, false);
    }
    if (!isnan(spec->critical_from)) {
        draw_arc(layer, center, radius, angle_of(gauge, spec->critical_from), end, red,
                 ZONE_OPACITY, false);
    }
    if (!isnan(spec->critical_below)) {
        draw_arc(layer, center, radius, SWEEP_START, angle_of(gauge, spec->critical_below), red,
                 ZONE_OPACITY, false);
    }
}

static void draw_marker(const SpGauge *gauge, lv_layer_t *layer, lv_point_t center, uint16_t radius)
{
    const float radians = angle_of(gauge, gauge->marker) * PI_F / 180.0f;
    const float inner = (float)radius - ARC_WIDTH / 2.0f - 5.0f;
    const float outer = (float)radius + ARC_WIDTH / 2.0f + 5.0f;
    lv_draw_line_dsc_t dsc;

    lv_draw_line_dsc_init(&dsc);
    dsc.color = SP_COLOR_TEXT;
    dsc.width = 3;
    dsc.round_start = dsc.round_end = 1;
    dsc.p1.x = SP_PRECISE((float)center.x + cosf(radians) * inner);
    dsc.p1.y = SP_PRECISE((float)center.y + sinf(radians) * inner);
    dsc.p2.x = SP_PRECISE((float)center.x + cosf(radians) * outer);
    dsc.p2.y = SP_PRECISE((float)center.y + sinf(radians) * outer);
    lv_draw_line(layer, &dsc);
}

static void on_draw(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target_obj(event);
    const SpGauge *gauge = lv_event_get_user_data(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_point_t center;
    uint16_t radius;

    lv_obj_get_coords(obj, &area);
    center.x = (area.x1 + area.x2) / 2;
    center.y = (area.y1 + area.y2) / 2;
    radius = (uint16_t)(lv_area_get_width(&area) / 2 - ARC_WIDTH / 2 - 8);

    draw_zones(gauge, layer, center, radius);
    if (gauge->valid) {
        draw_arc(layer, center, radius, SWEEP_START, angle_of(gauge, gauge->value),
                 sp_severity_color(gauge->severity), LV_OPA_COVER, true);
    }
    if (gauge->marker_visible) {
        draw_marker(gauge, layer, center, radius);
    }
}

static void on_delete(lv_event_t *event)
{
    lv_free(lv_event_get_user_data(event));
}

lv_obj_t *sp_gauge_create(lv_obj_t *parent, const SpGaugeSpec *spec, int32_t diameter)
{
    lv_obj_t *obj = lv_obj_create(parent);
    SpGauge *gauge = lv_malloc(sizeof *gauge);

    lv_memzero(gauge, sizeof *gauge);
    gauge->spec = *spec;
    gauge->severity = SP_SEVERITY_INACTIVE;
    sp_theme_make_plain(obj);
    lv_obj_set_size(obj, diameter, diameter);
    lv_obj_add_event_cb(obj, on_draw, LV_EVENT_DRAW_MAIN, gauge);
    lv_obj_add_event_cb(obj, on_delete, LV_EVENT_DELETE, gauge);
    lv_obj_set_user_data(obj, gauge);

    gauge->value_label = sp_label_create(obj, SP_FONT_DISPLAY, SP_COLOR_TEXT);
    lv_label_set_text(gauge->value_label, "--");
    lv_obj_align(gauge->value_label, LV_ALIGN_CENTER, 0, -6);
    gauge->unit_label = sp_label_create(obj, SP_FONT_BODY, SP_COLOR_TEXT_DIM);
    lv_label_set_text(gauge->unit_label, spec->unit);
    lv_obj_align(gauge->unit_label, LV_ALIGN_CENTER, 0, diameter / 5);
    return obj;
}

void sp_gauge_set_value(lv_obj_t *obj, float value, bool valid, const char *text,
                        SpSeverity severity)
{
    SpGauge *gauge = lv_obj_get_user_data(obj);

    gauge->value = value;
    gauge->valid = valid;
    gauge->severity = severity;
    lv_label_set_text(gauge->value_label, text);
    lv_obj_set_style_text_color(
        gauge->value_label,
        valid && severity >= SP_SEVERITY_WARNING ? sp_severity_color(severity) : SP_COLOR_TEXT, 0);
    lv_obj_align(gauge->value_label, LV_ALIGN_CENTER, 0, -6);
    lv_obj_invalidate(obj);
}

void sp_gauge_set_marker(lv_obj_t *obj, float value, bool visible)
{
    SpGauge *gauge = lv_obj_get_user_data(obj);

    gauge->marker = value;
    gauge->marker_visible = visible;
    lv_obj_invalidate(obj);
}

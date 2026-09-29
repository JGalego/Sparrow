#include "boiler_process_view.h"

#include <math.h>
#include <string.h>

#include "sp_theme.h"

#define REFRESH_MS          33
#define FLOW_DASH           10.0f
#define FLOW_GAP            10.0f
#define FLOW_SPEED_PX_PER_S 60.0f

typedef struct {
    BoilerViewModel model;
    lv_obj_t *temperature_label;
    lv_obj_t *pressure_label;
    lv_obj_t *heater_label;
    lv_obj_t *flow_label;
    lv_obj_t *valve_label;
    lv_obj_t *pump_label;
} ProcessView;

/* Geometry, relative to the view's top-left corner. */
enum {
    VESSEL_X = 70,
    VESSEL_Y = 52,
    VESSEL_W = 200,
    VESSEL_H = 200,
    VESSEL_RADIUS = 34,
    PIPE_WIDTH = 8,
    OUTLET_Y = 100,
    RETURN_Y = 214,
    LOOP_X = 400,
    VALVE_Y = 152,
    PUMP_X = 340
};

static lv_point_t at(const lv_area_t *origin, int32_t x, int32_t y)
{
    const lv_point_t point = {origin->x1 + x, origin->y1 + y};

    return point;
}

static lv_area_t rect(const lv_area_t *origin, int32_t x, int32_t y, int32_t w, int32_t h)
{
    const lv_area_t area = {origin->x1 + x, origin->y1 + y, origin->x1 + x + w - 1,
                            origin->y1 + y + h - 1};

    return area;
}

static lv_color_t mix(lv_color_t a, lv_color_t b, float fraction)
{
    return lv_color_mix(b, a, (uint8_t)(fminf(1.0f, fmaxf(0.0f, fraction)) * 255.0f));
}

static lv_color_t water_color(float temperature_c)
{
    return mix(lv_color_hex(0x2F74B5), lv_color_hex(0xE8A33D), (temperature_c - 65.0f) / 50.0f);
}

static void draw_line(lv_layer_t *layer, lv_point_t a, lv_point_t b, int32_t width,
                      lv_color_t color, lv_opa_t opa)
{
    lv_draw_line_dsc_t dsc;

    lv_draw_line_dsc_init(&dsc);
    dsc.p1.x = a.x;
    dsc.p1.y = a.y;
    dsc.p2.x = b.x;
    dsc.p2.y = b.y;
    dsc.width = width;
    dsc.color = color;
    dsc.opa = opa;
    lv_draw_line(layer, &dsc);
}

static void fill_rect(lv_layer_t *layer, const lv_area_t *area, int32_t radius, lv_color_t color,
                      lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = radius;
    lv_draw_rect(layer, &dsc, area);
}

static void outline_rect(lv_layer_t *layer, const lv_area_t *area, int32_t radius, lv_color_t color,
                         int32_t width)
{
    lv_draw_rect_dsc_t dsc;

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.radius = radius;
    dsc.border_width = width;
    dsc.border_color = color;
    dsc.border_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &dsc, area);
}

static void draw_vessel(lv_layer_t *layer, const lv_area_t *origin, const BoilerViewModel *m)
{
    const lv_area_t vessel = rect(origin, VESSEL_X, VESSEL_Y, VESSEL_W, VESSEL_H);
    const lv_area_t inner = rect(origin, VESSEL_X + 3, VESSEL_Y + 3, VESSEL_W - 6, VESSEL_H - 6);
    const int32_t level = (VESSEL_H - 6) * 78 / 100;
    const lv_area_t water_clip =
        rect(origin, VESSEL_X + 3, VESSEL_Y + 3 + (VESSEL_H - 6 - level), VESSEL_W - 6, level);
    const lv_area_t saved_clip = layer->_clip_area;
    lv_area_t clipped;

    fill_rect(layer, &vessel, VESSEL_RADIUS, lv_color_hex(0x10161D), LV_OPA_COVER);
    if (_lv_area_intersect(&clipped, &saved_clip, &water_clip)) {
        layer->_clip_area = clipped;
        /* The level is not measured; an unknown temperature only greys the water. */
        const lv_color_t water = m->temperature.valid ? water_color(m->temperature.value)
                                                      : sp_severity_color(SP_SEVERITY_INACTIVE);
        fill_rect(layer, &inner, VESSEL_RADIUS - 3, water, LV_OPA_80);
        layer->_clip_area = saved_clip;
        draw_line(layer, at(origin, VESSEL_X + 3, VESSEL_Y + 3 + (VESSEL_H - 6 - level)),
                  at(origin, VESSEL_X + VESSEL_W - 4, VESSEL_Y + 3 + (VESSEL_H - 6 - level)), 2,
                  lv_color_white(), LV_OPA_30);
    }
    outline_rect(layer, &vessel, VESSEL_RADIUS,
                 m->temperature.severity >= SP_SEVERITY_WARNING
                     ? sp_severity_color(m->temperature.severity)
                     : lv_color_hex(0x46566A),
                 3);
}

static void draw_heater(lv_layer_t *layer, const lv_area_t *origin, const BoilerViewModel *m,
                        uint32_t tick_ms)
{
    const int32_t left = VESSEL_X + 36;
    const int32_t right = VESSEL_X + VESSEL_W - 36;
    const int32_t base = VESSEL_Y + VESSEL_H - 26;
    const int32_t loops = 4;
    const int32_t pitch = (right - left) / loops;
    const float pulse = 0.75f + 0.25f * sinf((float)tick_ms / 300.0f);
    const float power = m->heater_on ? fmaxf(0.25f, m->heater_power.value / 100.0f) : 0.0f;
    const lv_color_t color =
        m->heater_on ? mix(lv_color_hex(0xC2571F), lv_color_hex(0xFFB14A), power * pulse)
                     : lv_color_hex(0x5B6675);

    for (int32_t i = 0; i < loops; i++) {
        const int32_t x = left + i * pitch;
        draw_line(layer, at(origin, x, base), at(origin, x + pitch / 2, base - 22), 4, color,
                  LV_OPA_COVER);
        draw_line(layer, at(origin, x + pitch / 2, base - 22), at(origin, x + pitch, base), 4,
                  color, LV_OPA_COVER);
    }
}

static void draw_pipe_path(lv_layer_t *layer, const lv_area_t *origin, lv_color_t color)
{
    const lv_point_t path[] = {
        at(origin, VESSEL_X + VESSEL_W - 2, OUTLET_Y),
        at(origin, LOOP_X, OUTLET_Y),
        at(origin, LOOP_X, RETURN_Y),
        at(origin, VESSEL_X + VESSEL_W - 2, RETURN_Y),
    };

    for (size_t i = 0; i + 1 < sizeof path / sizeof path[0]; i++) {
        draw_line(layer, path[i], path[i + 1], PIPE_WIDTH, color, LV_OPA_COVER);
    }
}

static void draw_flow_segment(lv_layer_t *layer, lv_point_t a, lv_point_t b, float phase,
                              lv_color_t color)
{
    const float length = hypotf((float)(b.x - a.x), (float)(b.y - a.y));
    const float dx = (float)(b.x - a.x) / length;
    const float dy = (float)(b.y - a.y) / length;

    for (float d = -fmodf(phase, FLOW_DASH + FLOW_GAP); d < length; d += FLOW_DASH + FLOW_GAP) {
        const float from = fmaxf(d, 0.0f);
        const float to = fminf(d + FLOW_DASH, length);
        if (to > from) {
            const lv_point_t p = {a.x + (int32_t)(dx * from), a.y + (int32_t)(dy * from)};
            const lv_point_t q = {a.x + (int32_t)(dx * to), a.y + (int32_t)(dy * to)};
            draw_line(layer, p, q, 3, color, LV_OPA_COVER);
        }
    }
}

static void draw_flow(lv_layer_t *layer, const lv_area_t *origin, const BoilerViewModel *m,
                      uint32_t tick_ms)
{
    const float phase = (float)tick_ms / 1000.0f * FLOW_SPEED_PX_PER_S;
    const lv_color_t color = mix(water_color(m->temperature.value), lv_color_white(), 0.35f);
    const lv_point_t path[] = {
        at(origin, VESSEL_X + VESSEL_W, OUTLET_Y),
        at(origin, LOOP_X, OUTLET_Y),
        at(origin, LOOP_X, RETURN_Y),
        at(origin, VESSEL_X + VESSEL_W, RETURN_Y),
    };
    float travelled = 0.0f;

    if (m->flow.value < 1.0f || !m->flow.valid) {
        return;
    }
    for (size_t i = 0; i + 1 < sizeof path / sizeof path[0]; i++) {
        const float length =
            hypotf((float)(path[i + 1].x - path[i].x), (float)(path[i + 1].y - path[i].y));
        draw_flow_segment(layer, path[i], path[i + 1], phase - travelled, color);
        travelled += length;
    }
}

static void draw_valve(lv_layer_t *layer, const lv_area_t *origin, const BoilerViewModel *m)
{
    const lv_color_t color =
        sp_severity_color(m->valve_severity == SP_SEVERITY_CRITICAL
                              ? SP_SEVERITY_CRITICAL
                              : (m->valve_open ? SP_SEVERITY_NORMAL : SP_SEVERITY_INACTIVE));
    const int32_t half = 15;
    lv_draw_triangle_dsc_t triangle;
    const lv_area_t body = rect(origin, LOOP_X - half - 2, VALVE_Y - half, 2 * half + 4, 2 * half);

    fill_rect(layer, &body, 6, SP_COLOR_SURFACE, LV_OPA_COVER);
    lv_draw_triangle_dsc_init(&triangle);
    triangle.bg_color = color;
    triangle.bg_opa = LV_OPA_COVER;
    triangle.p[0].x = origin->x1 + LOOP_X - half;
    triangle.p[0].y = origin->y1 + VALVE_Y - half + 4;
    triangle.p[1].x = origin->x1 + LOOP_X + half;
    triangle.p[1].y = origin->y1 + VALVE_Y - half + 4;
    triangle.p[2].x = origin->x1 + LOOP_X;
    triangle.p[2].y = origin->y1 + VALVE_Y;
    lv_draw_triangle(layer, &triangle);
    triangle.p[0].y = origin->y1 + VALVE_Y + half - 4;
    triangle.p[1].y = origin->y1 + VALVE_Y + half - 4;
    lv_draw_triangle(layer, &triangle);
    outline_rect(layer, &body, 6, color, 2);
}

static void draw_pump(lv_layer_t *layer, const lv_area_t *origin, const BoilerViewModel *m,
                      uint32_t tick_ms)
{
    const SpSeverity severity = m->pump_severity == SP_SEVERITY_CRITICAL
                                    ? SP_SEVERITY_CRITICAL
                                    : (m->pump_on ? SP_SEVERITY_NORMAL : SP_SEVERITY_INACTIVE);
    const lv_color_t color = sp_severity_color(severity);
    const lv_point_t center = at(origin, PUMP_X, RETURN_Y);
    const float angle = m->pump_on ? (float)tick_ms / 250.0f : 0.0f;
    lv_draw_arc_dsc_t ring;
    lv_draw_triangle_dsc_t blade;

    fill_rect(layer, &(lv_area_t){center.x - 22, center.y - 22, center.x + 22, center.y + 22}, 22,
              SP_COLOR_SURFACE, LV_OPA_COVER);
    lv_draw_arc_dsc_init(&ring);
    ring.center = center;
    ring.radius = 21;
    ring.width = 3;
    ring.start_angle = 0;
    ring.end_angle = 360;
    ring.color = color;
    lv_draw_arc(layer, &ring);

    lv_draw_triangle_dsc_init(&blade);
    blade.bg_color = color;
    blade.bg_opa = LV_OPA_COVER;
    for (int i = 0; i < 3; i++) {
        const float a = angle + (float)i * 2.0943951f;
        blade.p[0].x = center.x;
        blade.p[0].y = center.y;
        blade.p[1].x = center.x + SP_PRECISE(14.0f * cosf(a - 0.45f));
        blade.p[1].y = center.y + SP_PRECISE(14.0f * sinf(a - 0.45f));
        blade.p[2].x = center.x + SP_PRECISE(14.0f * cosf(a + 0.45f));
        blade.p[2].y = center.y + SP_PRECISE(14.0f * sinf(a + 0.45f));
        lv_draw_triangle(layer, &blade);
    }
}

static void on_draw(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target_obj(event);
    const ProcessView *view = lv_obj_get_user_data(obj);
    lv_layer_t *layer = lv_event_get_layer(event);
    const uint32_t tick_ms = lv_tick_get();
    lv_area_t origin;

    lv_obj_get_coords(obj, &origin);
    draw_pipe_path(layer, &origin, lv_color_hex(0x2C3947));
    draw_flow(layer, &origin, &view->model, tick_ms);
    draw_vessel(layer, &origin, &view->model);
    draw_heater(layer, &origin, &view->model, tick_ms);
    draw_valve(layer, &origin, &view->model);
    draw_pump(layer, &origin, &view->model, tick_ms);
}

static void on_animation_tick(lv_timer_t *timer)
{
    lv_obj_invalidate(lv_timer_get_user_data(timer));
}

static void on_delete(lv_event_t *event)
{
    lv_free(lv_event_get_user_data(event));
}

static lv_obj_t *add_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int32_t x,
                           int32_t y, int32_t width, lv_text_align_t align)
{
    lv_obj_t *label = sp_label_create(parent, font, color);

    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_text(label, "");
    return label;
}

lv_obj_t *boiler_process_view_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t width,
                                     int32_t height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    ProcessView *view = lv_malloc(sizeof *view);

    lv_memzero(view, sizeof *view);
    sp_theme_make_plain(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_user_data(obj, view);
    lv_obj_add_event_cb(obj, on_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, on_delete, LV_EVENT_DELETE, view);
    lv_timer_create(on_animation_tick, REFRESH_MS, obj);

    view->temperature_label = add_label(obj, SP_FONT_VALUE, SP_COLOR_TEXT, VESSEL_X, VESSEL_Y + 44,
                                        VESSEL_W, LV_TEXT_ALIGN_CENTER);
    view->pressure_label = add_label(obj, SP_FONT_BODY, SP_COLOR_TEXT, VESSEL_X, VESSEL_Y + 100,
                                     VESSEL_W, LV_TEXT_ALIGN_CENTER);
    view->heater_label = add_label(obj, SP_FONT_SMALL, SP_COLOR_TEXT_DIM, VESSEL_X, VESSEL_Y + 138,
                                   VESSEL_W, LV_TEXT_ALIGN_CENTER);
    view->flow_label = add_label(obj, SP_FONT_SMALL, SP_COLOR_TEXT_DIM, 296, OUTLET_Y - 24, 100,
                                 LV_TEXT_ALIGN_CENTER);
    view->valve_label = add_label(obj, SP_FONT_SMALL, SP_COLOR_TEXT_DIM, LOOP_X + 24, VALVE_Y - 8,
                                  70, LV_TEXT_ALIGN_LEFT);
    view->pump_label = add_label(obj, SP_FONT_SMALL, SP_COLOR_TEXT_DIM, PUMP_X - 30, RETURN_Y + 28,
                                 60, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(view->pump_label, "PUMP");
    return obj;
}

void boiler_process_view_update(lv_obj_t *obj, const BoilerViewModel *model)
{
    ProcessView *view = lv_obj_get_user_data(obj);
    const char *unit_c = "\xC2\xB0"
                         "C";

    view->model = *model;
    lv_label_set_text_fmt(view->temperature_label, "%s %s", model->temperature.text, unit_c);
    lv_obj_set_style_text_color(view->temperature_label,
                                model->temperature.severity >= SP_SEVERITY_WARNING
                                    ? sp_severity_color(model->temperature.severity)
                                    : SP_COLOR_TEXT,
                                0);
    lv_label_set_text_fmt(view->pressure_label, "%s bar", model->pressure.text);
    lv_label_set_text_fmt(view->heater_label, "HEATER  %s%%", model->heater_power.text);
    lv_label_set_text_fmt(view->flow_label, "%s L/min", model->flow.text);
    lv_label_set_text_fmt(view->valve_label, "VALVE\n%s%%", model->valve_position.text);
    lv_obj_invalidate(obj);
}

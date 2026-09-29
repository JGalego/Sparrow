#include "sp_trend.h"

#include <math.h>

#include "sp_theme.h"

#define AXIS_WIDTH       36
#define AXIS_LABELS      5
#define TIME_AXIS_HEIGHT 18
#define PLOT_TOP         8
#define CHART_SCALE      1000

typedef struct {
    SpTrendSpec spec;
    lv_obj_t *chart;
    lv_obj_t *span_label;
    lv_chart_series_t *series[SP_TREND_SERIES];
} SpTrend;

static void on_delete(lv_event_t *event)
{
    lv_free(lv_event_get_user_data(event));
}

static void create_axis_labels(lv_obj_t *parent, float min, float max, int32_t x, int32_t height,
                               lv_text_align_t align, const char *format, lv_color_t color)
{
    for (int i = 0; i < AXIS_LABELS; i++) {
        lv_obj_t *label = sp_label_create(parent, SP_FONT_SMALL, color);
        const float value = max - (max - min) * (float)i / (float)(AXIS_LABELS - 1);

        lv_label_set_text_fmt(label, format, (double)value);
        lv_obj_set_width(label, AXIS_WIDTH - 6);
        lv_obj_set_style_text_align(label, align, 0);
        lv_obj_set_pos(label, x, PLOT_TOP + (height - 1) * i / (AXIS_LABELS - 1) - 7);
    }
}

static lv_obj_t *create_chart(lv_obj_t *parent, int32_t width, int32_t height)
{
    lv_obj_t *chart = lv_chart_create(parent);

    lv_obj_set_pos(chart, AXIS_WIDTH, PLOT_TOP);
    lv_obj_set_size(chart, width - 2 * AXIS_WIDTH, height);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_SCALE);
    lv_chart_set_div_line_count(chart, AXIS_LABELS, 7);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_border_color(chart, SP_COLOR_BORDER, 0);
    lv_obj_set_style_radius(chart, 4, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_line_color(chart, SP_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    return chart;
}

lv_obj_t *sp_trend_create(lv_obj_t *parent, const SpTrendSpec *spec, int32_t x, int32_t y,
                          int32_t width, int32_t height)
{
    lv_obj_t *root = lv_obj_create(parent);
    SpTrend *trend = lv_malloc(sizeof *trend);
    const int32_t plot_height = height - TIME_AXIS_HEIGHT - PLOT_TOP;

    lv_memzero(trend, sizeof *trend);
    trend->spec = *spec;
    sp_theme_make_plain(root);
    lv_obj_set_pos(root, x, y);
    lv_obj_set_size(root, width, height);
    lv_obj_set_user_data(root, trend);
    lv_obj_add_event_cb(root, on_delete, LV_EVENT_DELETE, trend);

    trend->chart = create_chart(root, width, plot_height);
    lv_chart_set_point_count(trend->chart, 120);
    lv_obj_set_style_line_width(trend->chart, 2, LV_PART_ITEMS);
    for (int i = 0; i < SP_TREND_SERIES; i++) {
        trend->series[i] =
            lv_chart_add_series(trend->chart, spec->colors[i], LV_CHART_AXIS_PRIMARY_Y);
    }
    create_axis_labels(root, spec->left_min, spec->left_max, 0, plot_height, LV_TEXT_ALIGN_RIGHT,
                       "%.0f", spec->colors[0]);
    create_axis_labels(root, spec->right_min, spec->right_max, width - AXIS_WIDTH + 6, plot_height,
                       LV_TEXT_ALIGN_LEFT, "%.1f", spec->colors[2]);

    trend->span_label = sp_label_create(root, SP_FONT_SMALL, SP_COLOR_TEXT_DIM);
    lv_obj_set_pos(trend->span_label, AXIS_WIDTH, PLOT_TOP + plot_height + 3);
    lv_label_set_text(trend->span_label, "");
    return root;
}

static int32_t scaled(float value, float min, float max)
{
    if (isnan(value)) {
        return LV_CHART_POINT_NONE;
    }
    return (int32_t)lroundf(fminf(1.0f, fmaxf(0.0f, (value - min) / (max - min))) * CHART_SCALE);
}

void sp_trend_set_series(lv_obj_t *root, int series, const float *values, size_t count)
{
    SpTrend *trend = lv_obj_get_user_data(root);
    const bool right = series == 2;
    const float min = right ? trend->spec.right_min : trend->spec.left_min;
    const float max = right ? trend->spec.right_max : trend->spec.left_max;

    for (size_t i = 0; i < count; i++) {
        lv_chart_set_value_by_id(trend->chart, trend->series[series], (uint32_t)i,
                                 scaled(values[i], min, max));
    }
    lv_chart_refresh(trend->chart);
}

void sp_trend_set_span_label(lv_obj_t *root, const char *text)
{
    SpTrend *trend = lv_obj_get_user_data(root);

    lv_label_set_text(trend->span_label, text);
}

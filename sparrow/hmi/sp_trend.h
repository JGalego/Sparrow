#ifndef SPARROW_HMI_TREND_H
#define SPARROW_HMI_TREND_H

#include <stddef.h>

#include "lvgl.h"

#define SP_TREND_SERIES 3

typedef struct {
    float left_min;
    float left_max;
    float right_min;
    float right_max;
    lv_color_t colors[SP_TREND_SERIES];
    /* Series 0 and 1 use the left axis, series 2 the right axis. */
} SpTrendSpec;

/* A line chart with two value axes. Missing samples (NAN) leave gaps. */
lv_obj_t *sp_trend_create(lv_obj_t *parent, const SpTrendSpec *spec, int32_t x, int32_t y,
                          int32_t width, int32_t height);

/* Replaces one series with count evenly spaced values, oldest first. */
void sp_trend_set_series(lv_obj_t *trend, int series, const float *values, size_t count);

void sp_trend_set_span_label(lv_obj_t *trend, const char *text);

#endif

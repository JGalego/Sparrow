#ifndef SPARROW_HMI_GAUGE_H
#define SPARROW_HMI_GAUGE_H

#include <stdbool.h>

#include "lvgl.h"
#include "sp_severity.h"

/* Scale and colored zones of a gauge. Use NAN for a zone that does not exist. */
typedef struct {
    float min;
    float max;
    float warning_from;
    float critical_from;
    float critical_below;
    const char *unit;
} SpGaugeSpec;

/* A 270-degree arc gauge with a large numeric readout in the center. */
lv_obj_t *sp_gauge_create(lv_obj_t *parent, const SpGaugeSpec *spec, int32_t diameter);

/* text is the formatted readout. An invalid reading draws no value arc. */
void sp_gauge_set_value(lv_obj_t *gauge, float value, bool valid, const char *text,
                        SpSeverity severity);

/* Shows or hides a tick on the scale, used for the setpoint. */
void sp_gauge_set_marker(lv_obj_t *gauge, float value, bool visible);

#endif

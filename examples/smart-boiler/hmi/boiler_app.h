#ifndef BOILER_APP_H
#define BOILER_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boiler_model.h"
#include "boiler_notify.h"
#include "sp_ring.h"

#define BOILER_HISTORY_SECONDS 3600
#define BOILER_LINK_TIMEOUT_MS 2000
#define BOILER_CHART_POINTS    120

typedef enum {
    BOILER_CHART_2_MIN = 120,
    BOILER_CHART_10_MIN = 600,
    BOILER_CHART_60_MIN = 3600
} BoilerChartRange; /* span in 1 Hz samples */

typedef enum {
    BOILER_SERIES_TEMPERATURE,
    BOILER_SERIES_SETPOINT,
    BOILER_SERIES_PRESSURE
} BoilerSeries;

/*
 * Everything the HMI knows about the controller: the latest status frame,
 * one hour of history at 1 Hz, the notification log and link supervision.
 * It has no dependency on LVGL.
 */
typedef struct {
    BoilerStatus status;
    bool have_status;
    bool link_up;
    uint32_t last_frame_ms; /* local clock */
    uint32_t last_history_second;

    float temperature_samples[BOILER_HISTORY_SECONDS];
    float setpoint_samples[BOILER_HISTORY_SECONDS];
    float pressure_samples[BOILER_HISTORY_SECONDS];
    SpRing temperature_history;
    SpRing setpoint_history;
    SpRing pressure_history;

    BoilerNotifications notifications;
} BoilerApp;

void boiler_app_init(BoilerApp *app);

/* Takes a status frame received at local time now_ms. */
void boiler_app_ingest(BoilerApp *app, const BoilerStatus *status, uint32_t now_ms);

/* Supervises the link; call regularly with the local clock. */
void boiler_app_tick(BoilerApp *app, uint32_t now_ms);

/* Fills out[0..points) with the newest span samples of one series (NAN where unknown). */
size_t boiler_app_series(const BoilerApp *app, BoilerSeries series, BoilerChartRange range,
                         float *out, size_t points);

#endif

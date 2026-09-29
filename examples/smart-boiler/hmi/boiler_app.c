#include "boiler_app.h"

#include <math.h>
#include <string.h>

static SpRing *ring_for(BoilerApp *app, BoilerSeries series)
{
    switch (series) {
    case BOILER_SERIES_TEMPERATURE:
        return &app->temperature_history;
    case BOILER_SERIES_SETPOINT:
        return &app->setpoint_history;
    case BOILER_SERIES_PRESSURE:
        return &app->pressure_history;
    }
    return NULL;
}

void boiler_app_init(BoilerApp *app)
{
    memset(app, 0, sizeof *app);
    sp_ring_init(&app->temperature_history, app->temperature_samples, BOILER_HISTORY_SECONDS);
    sp_ring_init(&app->setpoint_history, app->setpoint_samples, BOILER_HISTORY_SECONDS);
    sp_ring_init(&app->pressure_history, app->pressure_samples, BOILER_HISTORY_SECONDS);
    boiler_notify_init(&app->notifications);
}

static void record_history(BoilerApp *app, const BoilerStatus *status)
{
    const uint32_t second = status->uptime_ms / 1000;

    if (app->have_status && second == app->last_history_second) {
        return;
    }
    app->last_history_second = second;
    sp_ring_push(&app->temperature_history,
                 status->temperature_valid ? status->temperature_c : NAN);
    sp_ring_push(&app->setpoint_history, status->setpoint_c);
    sp_ring_push(&app->pressure_history, status->pressure_valid ? status->pressure_bar : NAN);
}

static BoilerStatus quiet_status_like(const BoilerStatus *status)
{
    BoilerStatus quiet = {0};

    quiet.state = status->state;
    quiet.uptime_ms = status->uptime_ms;
    return quiet;
}

void boiler_app_ingest(BoilerApp *app, const BoilerStatus *status, uint32_t now_ms)
{
    const bool restored = app->have_status && !app->link_up;

    if (!app->have_status) {
        const BoilerStatus quiet = quiet_status_like(status);
        boiler_notify_add(&app->notifications, status->uptime_ms, SP_SEVERITY_NORMAL,
                          "Controller connected");
        boiler_notify_observe(&app->notifications, &quiet, status);
    } else {
        if (restored) {
            boiler_notify_add(&app->notifications, status->uptime_ms, SP_SEVERITY_NORMAL,
                              "Link restored");
        }
        boiler_notify_observe(&app->notifications, &app->status, status);
    }
    record_history(app, status);
    app->status = *status;
    app->have_status = true;
    app->link_up = true;
    app->last_frame_ms = now_ms;
}

void boiler_app_tick(BoilerApp *app, uint32_t now_ms)
{
    if (!app->have_status || !app->link_up) {
        return;
    }
    if (now_ms > app->last_frame_ms && now_ms - app->last_frame_ms > BOILER_LINK_TIMEOUT_MS) {
        app->link_up = false;
        boiler_notify_add(&app->notifications, app->status.uptime_ms, SP_SEVERITY_WARNING,
                          "Link lost");
    }
}

size_t boiler_app_series(const BoilerApp *app, BoilerSeries series, BoilerChartRange range,
                         float *out, size_t points)
{
    const SpRing *ring = ring_for((BoilerApp *)app, series);

    return sp_ring_downsample(ring, (size_t)range, out, points);
}

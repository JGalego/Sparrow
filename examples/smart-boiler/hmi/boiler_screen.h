#ifndef BOILER_SCREEN_H
#define BOILER_SCREEN_H

#include <stdbool.h>

#include "boiler_app.h"
#include "boiler_model.h"
#include "lvgl.h"

#define BOILER_SCREEN_WIDTH  1024
#define BOILER_SCREEN_HEIGHT 600

/* How the screen reaches the controller (and, in the simulator, the plant). */
typedef struct {
    void (*send_command)(void *context, const BoilerCommands *commands);
    void (*send_injection)(void *context, const BoilerFaultInjection *injection);
    void *context;
} BoilerScreenHost;

typedef struct BoilerScreen BoilerScreen;

/*
 * Builds the main screen under parent. The screen reads *app but never
 * modifies it. With sim_tools set, a SIM button opens a fault injection panel;
 * hosts without a simulator behind them leave it off.
 */
BoilerScreen *boiler_screen_create(lv_obj_t *parent, const BoilerApp *app,
                                   const BoilerScreenHost *host, bool sim_tools);

/* Refreshes every widget from the current app state. Cheap enough to call every frame. */
void boiler_screen_update(BoilerScreen *screen);

void boiler_screen_set_range(BoilerScreen *screen, BoilerChartRange range);

/* Shows the SIM panel (if sim_tools was enabled). */
void boiler_screen_show_sim_panel(BoilerScreen *screen, bool visible);

#endif
